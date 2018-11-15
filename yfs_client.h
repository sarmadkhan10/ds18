#include <pthread.h>
#include <assert.h>
#ifndef yfs_client_h
#define yfs_client_h
#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>
#include <random>

  class yfs_client {
  extent_client *ec;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    unsigned long long inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
  pthread_mutex_t m_;
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);
  inum ilookup(inum di, std::string name);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
  yfs_client::status createhelper(unsigned long long, const char*, unsigned long long *);
  int lookup(unsigned long long, const char*, unsigned long long*, int*);
  bool open_file(unsigned long long);
  int get_(unsigned long long , std::string &);
  int write_file(unsigned long long, const char*, size_t, off_t, bool);
  int read_file(unsigned long long, size_t, off_t, std::string &);
};

#endif 

#ifndef __FS_LOCK__
#define __FS_LOCK__

struct FSlock {
	private:
		pthread_mutex_t *m_;
	public:
		FSlock(pthread_mutex_t *m): m_(m) {
			assert(pthread_mutex_lock(m_)==0);
		}
		~FSlock() {
			assert(pthread_mutex_unlock(m_)==0);
		}
};
#endif  /*__FS_LOCK__*/
