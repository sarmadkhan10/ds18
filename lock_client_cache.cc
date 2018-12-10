// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>

static std::vector <cached_lock> cached_locks;
static std::list <lock_protocol::lockid_t> revoke_list;

static pthread_mutex_t cache_mutex;


cached_lock::cached_lock(lock_protocol::lockid_t lid_)
{
  assert(pthread_mutex_init(&lock_mutex, NULL) == 0);
  assert(pthread_cond_init(&release_wait, NULL) == 0);
  lid = lid_;
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
  pthread_t th;
  int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  assert (r == 0);

  assert(pthread_mutex_init(&cache_mutex, NULL) == 0);
}
/*
* finds the lock object to be found out in the cached_locks list
* 
* @param: lock id of the lock object to be found out in the cached_locks list
* @return: if found returns the lock object else null
*
*/
std::vector<cached_lock>::iterator
find_lock(lock_protocol::lockid_t lid)
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

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.

  int r;

  lock_protocol::status ret; 
	std::list<lock_protocol::lockid_t>::iterator it;
	std::vector<cached_lock>::iterator iter;
  

  assert(pthread_mutex_lock(&cache_mutex) == 0);

  while(1)
  {
    

  	for (it = revoke_list.begin(); it != revoke_list.end(); ) {

      iter = find_lock((*it));

      //prevents from another thread entering acquire()
      assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);
      assert(iter != cached_locks.end());

      if((*iter).state == FREE){
        revoke_list.erase(it);
        (*iter).state = NONE;
        ret = cl->call(lock_protocol::release, lock_client_cache::id, (*iter).lid, r);
        assert(ret == lock_protocol::OK);
        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
        //delete the object?
      } else{
        it++;
        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
        }
    }

  //if list empty sleep, only revoker should wake it up
  pthread_cond_wait(&((*iter).release_wait), &cache_mutex);
  }
}
/*
* attains the cache_mutex to avoid lost signals, finds the lock in cached_locks,
* locks the lock_mutex to avoid another thread entering acquire(), pushes to revoke 
* signals releaser, other threads waiting before 
*
*/
void
lock_client_cache::revoke(lock_protocol::lockid_t lid)
{
	std::vector<cached_lock>::iterator iter;

  assert(pthread_mutex_lock(&cache_mutex) == 0);

  iter = find_lock(lid);
  assert(iter != cached_locks.end());

  //so that another thread will not proceed in acquire()
  assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);

  revoke_list.push_back(lid);
  
  //threads waiting before the revoke called will still wake up 
  //releaser thread will also wake up
  pthread_cond_signal(&((*iter).release_wait));

  assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
  assert(pthread_mutex_unlock(&cache_mutex) == 0);

}
void 
lock_client_cache::retry(lock_protocol::lockid_t lid)
{

  lock_protocol::status ret ; 
  std::vector<cached_lock>::iterator iter;

  iter = find_lock(lid);

   if(ret == lock_protocol::OK){
      pthread_cond_signal(&((*iter).release_wait));
   }
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
  lock_protocol::status ret; 
  int r;
  
  while(1)
  {
    assert(pthread_mutex_lock(&cache_mutex) == 0); 
    //search LOCK_LIST to find the lock
	  std::vector<cached_lock>::iterator iter;
    iter = find_lock(lid);
    if(iter == cached_locks.end() || (*iter).state == NONE) 
    {
      //create a new lock and push it to cached_locks list
      cached_lock new_lock(lid);
      new_lock.state = ACQUIRING;
      cached_locks.push_back(new_lock);
      
      assert(pthread_mutex_lock(&(new_lock.lock_mutex)) == 0);
      assert(pthread_mutex_unlock(&cache_mutex) == 0);
      ret = cl->call(lock_protocol::acquire, lock_client_cache::id, lid, r);
      while(ret == lock_protocol::RETRY){
        //assuming the lock's mutex is released within the cond_wait
        pthread_cond_wait(&((*iter).release_wait), &((*iter).lock_mutex));
        ret = cl->call(lock_protocol::acquire, lock_client_cache::id, lid, r);
        if(ret == lock_protocol::OK){
          iter = find_lock(lid);
          (*iter).state = LOCKED;
          assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
          return lock_protocol::OK;
        }
      }
      if(ret == lock_protocol::OK){
        iter = find_lock(lid);
        (*iter).state = LOCKED;
        return lock_protocol::OK;
      }
    } else{
      assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);
      auto it = std::find(revoke_list.begin(), revoke_list.end(), lid); //unnecessary
      if(it == revoke_list.end() && (*iter).state == FREE)
      {
        (*iter).state = LOCKED;
        assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);
        return lock_protocol::OK;
      }
      else if(it != revoke_list.end())
        continue;
      else if((*iter).state ==  LOCKED){
        pthread_cond_wait(&((*iter).release_wait), &((*iter).lock_mutex));
      }
      else if((*iter).state ==  ACQUIRING){
        pthread_cond_wait(&((*iter).release_wait), &((*iter).lock_mutex));
      }
      else if((*iter).state ==  RELEASING){
        continue;
      }
    }
  }
 // return lock_protocol::RPCERR;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	std::vector<cached_lock>::iterator iter;

  assert(pthread_mutex_lock(&cache_mutex) == 0);

  iter = find_lock(lid);

  assert(iter != cached_locks.end());
  assert(pthread_mutex_lock(&((*iter).lock_mutex)) == 0);
  (*iter).state = FREE;
  assert(pthread_mutex_unlock(&((*iter).lock_mutex)) == 0);

  assert(pthread_mutex_lock(&cache_mutex) == 0);
  //assuming ordering is maintained, threads waiting before the revoke will still 
  //get the lock
  pthread_cond_signal(&((*iter).release_wait));

  return lock_protocol::OK;
}

