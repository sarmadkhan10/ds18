// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

static pthread_cond_t cond_revoke;
static pthread_cond_t cond_retry;

using namespace std;

static pthread_mutex_t mutex_;

static void *
revokethread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->retryer();
  return 0;
}

lock_server_cache::lock_server_cache()
{
  assert(pthread_mutex_init(&mutex_, NULL) == 0);
  assert(pthread_cond_init(&cond_retry, NULL) == 0);
  assert(pthread_cond_init(&cond_revoke, NULL) == 0);

  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  assert (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  assert (r == 0);

  cout << "server starts" << endl;
}

void
lock_server_cache::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
  
  std::map<lock_protocol::lockid_t, std::pair<std::string, bool>>::iterator iter;

  pthread_mutex_lock(&mutex_);

  while(1) {
    for(iter = map_revoke.begin(); iter!= map_revoke.end(); iter++) {
      // check if revoke has already been called
      if(iter->second.second == false) {
        iter->second.second = true;
        lock_protocol::lockid_t rev_lid = iter->first;
        string rev_cid = iter->second.first;

        //pthread_mutex_unlock(&mutex_);

        assert(map_client.find(rev_cid) != map_client.end());

        rpcc *cl = map_client[rev_cid];

        int r;
        int ret = cl->call(rlock_protocol::revoke, rev_lid, r);
        assert (ret == lock_protocol::OK);

        cout << "revoke rpc done: " << rev_lid << " cid: " << rev_cid << endl;

        //pthread_mutex_lock(&mutex_);
      }
    }

    pthread_cond_wait(&cond_revoke, &mutex_);
  }
}


void
lock_server_cache::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.

  pthread_mutex_lock(&mutex_);

  while(1) {
    while(map_retry.empty()) {
      pthread_cond_wait(&cond_retry, &mutex_);
    }

    //assert(!map_retry.empty());

    map<lock_protocol::lockid_t, std::vector<string>>::iterator it;
    it = map_retry.begin();

    lock_protocol::lockid_t retry_lid = it->first;
    vector<string> retry_cid_list = it->second;

    map_retry.erase(it);

    pthread_mutex_unlock(&mutex_);

    for(vector<string>::iterator it = retry_cid_list.begin(); it!=retry_cid_list.end(); it++) {
      assert(map_client.find(*it) != map_client.end());
      rpcc *cl = map_client[*it];

      int r;
      assert(cl->call(rlock_protocol::retry, retry_lid, r) == lock_protocol::OK);
    }

    pthread_mutex_lock(&mutex_);
  }
}


lock_protocol::status lock_server_cache::acquire(string cid, lock_protocol::lockid_t lid,
                                                 int &r)
{
  cout << "lock_server_cache acquire enter" << endl;
  assert(pthread_mutex_lock(&mutex_) == 0);

  // add the client to map if seen for the first time
  if(map_client.find(cid) == map_client.end()) {
    cout << "adding client: " << cid << endl;
    sockaddr_in dstsock;
    make_sockaddr(cid.c_str(), &dstsock);
    rpcc *cl = new rpcc(dstsock);
    if (cl->bind() < 0) {
      printf("lock_server_cache: call bind failed\n");
    }

    map_client[cid] = cl;
  }

  map<lock_protocol::lockid_t, string>::iterator it;

  it = map_lock.find(lid);

  // if the lock doesn't exist
  if(it == map_lock.end()) {
    // ownership of lock
    map_lock[lid] = cid;

    assert(pthread_mutex_unlock(&mutex_) == 0);

    return lock_protocol::OK;
  }
  // if the lock already exists
  else {
    // if no client holds the lock
    if(it->second == "") {
      map_lock[lid] = cid;

      assert(pthread_mutex_unlock(&mutex_) == 0);

      cout << "no client holds lock. awarding ownership" << endl;
      return lock_protocol::OK;
    }
    // some client holds the lock. will retry later
    else {
      // if seen for the first time
      if(map_retry.find(lid) == map_retry.end()) {
        vector<std::string> new_retry_cid_list;
        map_retry[lid] = new_retry_cid_list;
      }

      // TODO: fix?
      map_retry.find(lid)->second.push_back(cid);

      // check if revoke rpc should be called or not
      std::map<lock_protocol::lockid_t, std::pair<std::string, bool>>::iterator it_rev;
      it_rev = map_revoke.find(lid);
      if(it_rev == map_revoke.end()) {
        map_revoke[lid] = make_pair(it->second, false);

      pthread_cond_signal(&cond_revoke);}

      assert(pthread_mutex_unlock(&mutex_) == 0);

      return lock_protocol::RETRY;
    }
  }
}

lock_protocol::status lock_server_cache::release(string cid, lock_protocol::lockid_t lid, int &r)
{
  cout << "lock_server_cache release enter: " << cid << endl;
  pthread_mutex_lock(&mutex_);

  // check if the releaser actually holds the lock
  bool releaser_holds = false;
  if(map_lock.find(lid) != map_lock.end()) {
    if(map_lock[lid] == cid) {
      releaser_holds = true;
    }
  }
  if(!releaser_holds) return lock_protocol::RPCERR;

  // remove client id
  map_lock[lid] = "";

  // remove from map_revoke
  std::map<lock_protocol::lockid_t, std::pair<std::string, bool>>::iterator it_rev;
  it_rev = map_revoke.find(lid);
  assert(it_rev != map_revoke.end());
  map_revoke.erase(it_rev);

  pthread_cond_signal(&cond_retry);

  pthread_mutex_unlock(&mutex_);


  cout << "lock_server_cache release exit" << endl;

  return lock_protocol::OK;
}
