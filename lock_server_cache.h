#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"
#include <vector>


class lock_server_cache {
 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
  lock_protocol::status acquire(lock_protocol::lockid_t lid,
								std::string cid, int &r);
  lock_protocol::status release(lock_protocol::lockid_t lid, int &r);

 private:
	std::map<lock_protocol::lockid_t, std::string> map_lock;
	std::map<lock_protocol::lockid_t, std::vector<std::string>> map_retry;
	std::map<lock_protocol::lockid_t, std::string> map_revoke;
	std::map<std::string, rpcc*> map_client;
};

#endif
