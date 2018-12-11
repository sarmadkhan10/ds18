// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>

using namespace std;


cached_lock::cached_lock(lock_protocol::lockid_t lid_)
{
  assert(pthread_mutex_init(&lock_mutex, NULL) == 0);
  assert(pthread_cond_init(&revoke_wait, NULL) == 0);
  lid = lid_;
  state = lock_client_cache::NONE;
}

static void *
releasethread(void *x)
{
  lock_client_cache *cc = (lock_client_cache *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  assert(pthread_cond_init(&release_wait, NULL) == 0);
  assert(pthread_mutex_init(&cache_mutex, NULL) == 0);

  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // assert(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  /* register RPC handlers with rlsrpc */
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry);

  pthread_t th;
  int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  assert (r == 0);
}
/*
* finds the lock object to be found out in the cached_locks list
* 
* @param: lock id of the lock object to be found out in the cached_locks list
* @return: if found returns the lock object else null
*
*/
std::vector<cached_lock>::iterator
lock_client_cache::find_lock(lock_protocol::lockid_t lid)
{
  std::vector<cached_lock>::iterator it;

  for(it = cached_locks.begin(); it != cached_locks.end(); it++)
  {
    if((*it).get_lid() == lid)
      return it;
  }
  return it;
}
/*
*
*/
void
lock_client_cache::releaser()
{
  int r;

  lock_protocol::status ret; 
	std::list<lock_protocol::lockid_t>::iterator it;
	std::vector<cached_lock>::iterator iter;

  assert(pthread_mutex_lock(&cache_mutex) == 0);

  while(1) {
    cout << "releaser enter" << endl;
  	for (it = revoke_list.begin(); it != revoke_list.end(); ) {

      iter = find_lock((*it));

      //prevents from another thread entering acquire()
      assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);
      assert(iter != cached_locks.end());

      if((*iter).state == FREE){
        it = revoke_list.erase(it);
        (*iter).state = RELEASING;

        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);

        ret = cl->call(lock_protocol::release, id, (*iter).get_lid(), r);
        assert(ret == lock_protocol::OK);

        // set state to NONE
        assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);
        (*iter).state = NONE;

        cout << "releaser released" << endl;
        //delete the object?
      } else {
        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
        it++;
      }
    }

    //if list empty sleep, only revoker should wake it up
    pthread_cond_wait(&release_wait, &cache_mutex);

    cout << "releaser wakes up" << endl;
  }
}
/*
* attains the cache_mutex to avoid lost signals, finds the lock in cached_locks,
* locks the lock_mutex to avoid another thread entering acquire(), pushes to revoke 
* signals releaser, other threads waiting before 
*
*/
lock_protocol::status
lock_client_cache::revoke(lock_protocol::lockid_t lid, int &r)
{
  cout << "revoke enter: " << lid << endl;
	std::vector<cached_lock>::iterator iter;

  assert(pthread_mutex_lock(&cache_mutex) == 0);

  iter = find_lock(lid);
  assert(iter != cached_locks.end());

  //so that another thread will not proceed in acquire()
  assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);

  revoke_list.push_back(lid);
  
  //threads waiting before the revoke called will still wake up 
  //releaser thread will also wake up
  pthread_cond_signal(&((*iter).revoke_wait));

  assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
  assert(pthread_mutex_unlock(&cache_mutex) == 0);

  cout << "revoke exit: " << lid << endl;

  return lock_protocol::OK;
}

lock_protocol::status 
lock_client_cache::retry(lock_protocol::lockid_t lid, int &r)
{
  cout << "retry enter: " << lid << endl;
  lock_protocol::status ret ; 
  std::vector<cached_lock>::iterator iter;

  iter = find_lock(lid);

  pthread_cond_signal(&((*iter).revoke_wait));

  cout << "retry exit: " << lid << endl;

  return lock_protocol::OK;
}
/*

*  if no element found ->  create a new lock object, acquire that lock from 
*  server, change the state, wait if server replies RETRY, 
*  else if it gets the lock return status

*  if lock exists and not in revoke list give the lock

*   STRONG INVARIANT - once lock is in revoke list, no other thread waits for it in 
*   cond_wait() 

*   TODO: change status ret
*/
lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  cout << "lock_client_cache acquire enter. lid: " << lid << endl;
  lock_protocol::status ret; 
  int r;
  
  while(1) {
    assert(pthread_mutex_lock(&cache_mutex) == 0);

    //search cached_locks to find the lock
	  std::vector<cached_lock>::iterator iter;
    iter = find_lock(lid);

    // if the cached_lock doesn't exist
    if(iter == cached_locks.end()) {
      //create a new lock and push it to cached_locks list
      cached_lock new_lock(lid);
      cached_locks.push_back(new_lock);

      iter = find_lock(lid);
    }

    if((*iter).state == NONE) {
      assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);

      (*iter).state = ACQUIRING;

      // while holding lock_mutex. deadlock?
      assert(pthread_mutex_unlock(&cache_mutex) == 0);

      ret = cl->call(lock_protocol::acquire, id, lid, r);

      cout << "ret acquire rpc: " << ret << endl;
      while(ret == lock_protocol::RETRY) {
        cout << "will retry later: " << lid << endl;
        //assuming the lock's mutex is released within the cond_wait
        pthread_cond_wait(&((*iter).revoke_wait), &((*iter).lock_mutex));
        cout << "retrying: " << lid << endl;
        ret = cl->call(lock_protocol::acquire, id, lid, r);

        if(ret == lock_protocol::OK) {
          cout << "retry success: " << lid << endl;
          break;
        }
      }

      assert(ret == lock_protocol::OK);

      (*iter).state = LOCKED;
      assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
      cout << "got lock" << endl;
      return lock_protocol::OK;

    } else {
      assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);

      std::list<lock_protocol::lockid_t>::iterator it;
      it = std::find(revoke_list.begin(), revoke_list.end(), lid); //unnecessary

      if (it != revoke_list.end()) {
        cout << "blah blah blah blah" << endl;
      }

      if(it == revoke_list.end() && (*iter).state == FREE) {
        (*iter).state = LOCKED;
        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
        assert(pthread_mutex_unlock(&cache_mutex) == 0);
        return lock_protocol::OK;
      }
      else if(it != revoke_list.end()) {
        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
        assert(pthread_mutex_unlock(&cache_mutex) == 0);
        continue;
      }
      else if((*iter).state ==  LOCKED) {
        assert(pthread_mutex_unlock(&cache_mutex) == 0);
        pthread_cond_wait(&((*iter).revoke_wait), &((*iter).lock_mutex));
      }
      else if((*iter).state ==  ACQUIRING) {
        assert(pthread_mutex_unlock(&cache_mutex) == 0);
        pthread_cond_wait(&((*iter).revoke_wait), &((*iter).lock_mutex));
      }
      else if((*iter).state ==  RELEASING) {
        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
        assert(pthread_mutex_unlock(&cache_mutex) == 0);
        continue;
      }
    }
  }

  cout << "lock_client_cache acquire exit" << endl;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  cout << "lock_client_cache release enter" << endl;
	std::vector<cached_lock>::iterator iter;

  assert(pthread_mutex_lock(&cache_mutex) == 0);

  iter = find_lock(lid);

  assert(iter != cached_locks.end());
  assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);
  (*iter).state = FREE;

  //assuming ordering is maintained, threads waiting before the revoke will still 
  //get the lock
  pthread_cond_signal(&release_wait);
  assert(pthread_mutex_unlock(&cache_mutex) == 0);

  pthread_cond_signal(&((*iter).revoke_wait));

  assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);

  cout << "lock_client_cache release exit" << endl;

  return lock_protocol::OK;
}

