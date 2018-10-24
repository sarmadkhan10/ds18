// this is the lock server
// the lock client has a similar interface

enum {
  FREE, LOCKED
};

#ifndef lock_server_h
#define lock_server_h


#include <string>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

class lock{
  lockid_t lock_id;
  int STATE;
  int lock_waiting;

  public:
    int get_lock_id(return lock_id;)
    int get_state(return state;)
    int get_lock_waiting(return lock_waiting;)

    void set_state_locked(){STATE = LOCKED;}
    void set_state_free(){STATE = FREE;}
    int inc_lock_waiting(return ++lock_waiting;)
    int dec_lock_waiting(return --lock_waiting;)
  
    lock_protocol::status acquire(lock_protocol::lockid_t);
    lock_protocol::status release(lock_protocol::lockid_t);
};

class lock_server {

 protected:
  int nacquire;

 public:
  lock_server();
  ~lock_server() {};
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
};

#endif 







