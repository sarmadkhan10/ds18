// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

pthread_mutex_t mutex_;
pthread_cond_t lock_free;

vector<lock *> list_locks;

lock::lock():
  state(FREE), lock_waiting(0)
{
}

lock *get_lock(lock_protocol::lockid_t lid)
{
  for(vector<lock *>::iterator lock_it = list_locks.begin();
        lock_it != list_locks.end(); ++lock_it)
  {
    lock *l = *lock_it;
    if(lid == l->get_lock_id())
      return l;
  }
  return NULL;
}

bool delete_lock(lock_protocol::lockid_t lid) {
  int i = 0;
  bool found = false;

  for(vector<lock *>::iterator lock_it = list_locks.begin();
      lock_it != list_locks.end(); ++lock_it)
  {
    lock *l = *lock_it;

    if(lid == l->get_lock_id()) {
      found = true;
      break;
    }
    i++;
  }

  if(found) {
    lock *l = *(list_locks.begin() + i);
    list_locks.erase(list_locks.begin() + i);

    free (l);
  }

  return found;
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

  lock *l = get_lock(lid);

  if(l != NULL) {
    while(1) {
      if(l->get_state() == FREE) {
        l->set_state_locked();
        assert(pthread_mutex_unlock(&mutex_) == 0);

        return lock_protocol::OK;
      }
      else {
        (void) l->inc_lock_waiting();
        pthread_cond_wait(&lock_free, &mutex_);
        (void) l->dec_lock_waiting();
      }
    }
  }
  else {
    // create a new lock
    lock *new_lock = new lock();
    new_lock->set_state_locked();
    new_lock->set_lock_id(lid);

    list_locks.push_back(new_lock);

    assert(pthread_mutex_unlock(&mutex_) == 0);

    return lock_protocol::OK;
  }
}

lock_protocol::status lock::release(lock_protocol::lockid_t lid) {
  assert(pthread_mutex_lock(&mutex_) == 0);

  lock *l = get_lock(lid);

  assert(l != NULL);

  // if no thread is waiting for the lock, delete it
  if (l->get_lock_waiting() == 0) {

  }

  return lock_protocol::OK;
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}
