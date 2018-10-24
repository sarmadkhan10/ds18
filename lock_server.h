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
  lock_protocol::lockid_t lock_id;
  int state;
  int lock_waiting;

  public:
    lock();

    int get_lock_id() {return this->lock_id;}
    int get_state() {return this->state;}
    int get_lock_waiting() {return this->lock_waiting;}
    int get_lock(lock_protocol::lockid_t lid);

    void set_state_locked(){this->state = LOCKED;}
    void set_state_free(){this->state = FREE;}
    int inc_lock_waiting() {this->lock_waiting++; return this->lock_waiting;}
    int dec_lock_waiting() {this->lock_waiting--; return this->lock_waiting;}
    void set_lock_id(lock_protocol::lockid_t lid) {this->lock_id = lid;}

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
