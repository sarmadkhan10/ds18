// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>


pthread_mutex_t mutex_;
pthread_cond_t lock_free;

vector<lock*> list_locks;

lock::lock(): 
  STATE(FREE), lock_waiting(0)
{
}

lock* get_lock(lockid_t lid)
{
  for(vector<lock *>::iterator lock_it = list_locks.begin();
        lock_it != list_locks.end(); ++lock_it)
  {
    lock *l = *lock_it;
    if(lid == l->lock_id) 
      return l;
  }
  return nullptr; 
}

lock_server::lock_server():
  nacquire (0) 
{

  pthread_mutex_init(&mutex_, 0);
  pthread_cond_init(&lock_free, 0);

}

lock_protocol::status lock::acquire(lock_protocol::lockid_t lid)
{
 assert(pthread_mutex_lock(&mutex_) == 0);
   lock *l;
   l = get_lock(lid);
   if(l){
    while(1){
      if(l->get_state() == FREE){
        l->set_state_locked(); 
        assert(pthread_mutex_unlock(&mutex_) == 0);
        return lock_protocol::OK;
      } else{
        assert(pthread_mutex_unlock(&mutex_) == 0);


    }   
   }
  
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}


