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
}

void
lock_server_cache::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock

  while(1) {
    pthread_mutex_lock(&mutex_);

    // if nothing to be done, wait
    if(map_revoke.empty()) {
      pthread_cond_wait(&cond_revoke, &mutex_);
    }
    else {
      map<lock_protocol::lockid_t, string>::iterator it;
      it = map_revoke.begin();

      lock_protocol::lockid_t rev_lid = it->first;
      string rev_cid = it->second;

      map_revoke.erase(it);

      pthread_mutex_unlock(&mutex_);

      rpcc *cl = map_client[rev_cid];

      int r;
      int ret = cl->call(rlock_protocol::revoke, rev_lid, r);
      assert (ret == lock_protocol::OK);
    }
  }
}


void
lock_server_cache::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.

  while(1) {
    pthread_mutex_lock(&mutex_);

    if(map_retry.empty()) {
      pthread_cond_wait(&cond_retry, &mutex_);
    }
    else {
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
    }
  }
}


lock_protocol::status lock_server_cache::acquire(lock_protocol::lockid_t lid,
                                                  string cid, int &r)
{
  assert(pthread_mutex_lock(&mutex_) == 0);

  // add the client to map if seen for the first time
  if(map_client.find(cid) == map_client.end()) {
    sockaddr_in dstsock;
    make_sockaddr(cid.c_str(), &dstsock);
    rpcc *cl = new rpcc(dstsock);

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

      return lock_protocol::OK;
    }
    // some client holds the lock. will retry later
    else {
      map_retry.find(lid)->second.push_back(cid);
      map_revoke[lid] = it->second;

      assert(pthread_mutex_unlock(&mutex_) == 0);

      pthread_cond_signal(&cond_revoke);

      return lock_protocol::RETRY;
    }
  }
}

lock_protocol::status lock_server_cache::release(lock_protocol::lockid_t lid, int &r) {
  pthread_mutex_lock(&mutex_);

  // remove client id
  map_lock[lid] = "";

  pthread_mutex_unlock(&mutex_);

  pthread_cond_signal(&cond_retry);

  return lock_protocol::OK;
}
