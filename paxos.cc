#include "paxos.h"
#include "handle.h"
// #include <signal.h>
#include <stdio.h>

// This module implements the proposer and acceptor of the Paxos
// distributed algorithm as described by Lamport's "Paxos Made
// Simple".  To kick off an instance of Paxos, the caller supplies a
// list of nodes, a proposed value, and invokes the proposer.  If the
// majority of the nodes agree on the proposed value after running
// this instance of Paxos, the acceptor invokes the upcall
// paxos_commit to inform higher layers of the agreed value for this
// instance.

using namespace std;


bool
operator> (const prop_t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m > b.m));
}

bool
operator>= (const prop_t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m >= b.m));
}

std::string
print_members(const std::vector<std::string> &nodes)
{
  std::string s;
  s.clear();
  for (unsigned i = 0; i < nodes.size(); i++) {
    s += nodes[i];
    if (i < (nodes.size()-1))
      s += ",";
  }
  return s;
}

bool isamember(std::string m, const std::vector<std::string> &nodes)
{
  for (unsigned i = 0; i < nodes.size(); i++) {
    if (nodes[i] == m) return 1;
  }
  return 0;
}

bool
proposer::isrunning()
{
  bool r;
  assert(pthread_mutex_lock(&pxs_mutex)==0);
  r = !stable;
  assert(pthread_mutex_unlock(&pxs_mutex)==0);
  return r;
}

// check if the servers in l2 contains a majority of servers in l1
bool
proposer::majority(const std::vector<std::string> &l1, 
		const std::vector<std::string> &l2)
{
  unsigned n = 0;

  for (unsigned i = 0; i < l1.size(); i++) {
    if (isamember(l1[i], l2))
      n++;
  }
  return n >= (l1.size() >> 1) + 1;
}

proposer::proposer(class paxos_change *_cfg, class acceptor *_acceptor, 
		   std::string _me)
  : cfg(_cfg), acc (_acceptor), me (_me), break1 (false), break2 (false), 
    stable (true)
{
  assert (pthread_mutex_init(&pxs_mutex, NULL) == 0);

}

void
proposer::setn()
{
  my_n.n = acc->get_n_h().n + 1 > my_n.n + 1 ? acc->get_n_h().n + 1 : my_n.n + 1;

  //
  my_n.m = me;
}

bool
proposer::run(int instance, std::vector<std::string> c_nodes, std::string c_v)
{
  cout << "proposer run: " << c_v << endl;
  std::vector<std::string> accepts;
  std::vector<std::string> nodes;
  std::vector<std::string> nodes1;
  std::string v;
  bool r = false;

  pthread_mutex_lock(&pxs_mutex);
  printf("start: initiate paxos for %s w. i=%d v=%s stable=%d\n",
	 print_members(c_nodes).c_str(), instance, c_v.c_str(), stable);
  if (!stable) {  // already running proposer?
    printf("proposer::run: already running\n");
    pthread_mutex_unlock(&pxs_mutex);
    return false;
  }
  setn();
  accepts.clear();
  nodes.clear();
  v.clear();
  nodes = c_nodes;
  if (prepare(instance, accepts, nodes, v)) {
    cout << "prepare success" << endl;

    if (majority(c_nodes, accepts)) {
      printf("paxos::manager: received a majority of prepare responses\n");

      if (v.size() == 0) {
        v = c_v;
      }
      else
        cout << "cannot propose a new value. using returned one: " << v << endl;

      breakpoint1();

      nodes1 = accepts;
      accepts.clear();
      accept(instance, accepts, nodes1, v);

      if (majority(c_nodes, accepts)) {
        printf("paxos::manager: received a majority of accept responses\n");

        breakpoint2();

	std::cout << "DECIDE calling\n";
        decide(instance, accepts, v);
	std::cout << "DECIDE RETURNED \n";
        r = true;
      } else {
        printf("paxos::manager: no majority of accept responses\n");
      }
    } else {
      printf("paxos::manager: no majority of prepare responses\n");
    }
  } else {
    printf("paxos::manager: prepare is rejected %d\n", stable);
  }
  stable = true;
  pthread_mutex_unlock(&pxs_mutex);
  return r;
}

bool
proposer::prepare(unsigned instance, std::vector<std::string> &accepts, 
         std::vector<std::string> nodes,
         std::string &v)
{
  std::vector<std::string>::iterator it;
  prop_t highest;
  highest.n = 0;

  for(it = nodes.begin(); it != nodes.end(); it++) {
    // preparereq rpc to every node
    paxos_protocol::preparearg prep_a;
    paxos_protocol::prepareres prep_r;

    prep_a.n = my_n;
    prep_a.instance = instance;

    handle h(*it);

    if(h.get_rpcc() != NULL) {
      int ret = h.get_rpcc()->call(paxos_protocol::preparereq, me, prep_a, prep_r, rpcc::to(1000));

      cout << "preparerep rpc done: " << *it << " ret: " << ret << endl;

      if(ret == paxos_protocol::OK) {
        if(prep_r.accept) {
          accepts.push_back(*it);

          if(!prep_r.v_a.empty()) {
            cout << "prepres not empty" << endl;

            if(prep_r.n_a > highest || highest.n == 0) {
              highest = prep_r.n_a;
              v = prep_r.v_a;
              cout << "highest na: " << prep_r.n_a.n << " va" << v << endl; 
            }
          }
        } else {
          // old instarnce. call commit. done
          stable = true;

          cout << "acc commit" << endl;
	  cout << "instance: " << prep_r.oldinstance << "view : " << prep_r.v_a << endl;
          acc->commit(prep_r.oldinstance, prep_r.v_a);
        }
        // reject?
      }
    }
  }

  return true;
}


void
proposer::accept(unsigned instance, std::vector<std::string> &accepts,
        std::vector<std::string> nodes, std::string v)
{
  cout << "accept start" << endl;
  std::vector<std::string>::iterator it;

  for(it = nodes.begin(); it != nodes.end(); it++) {
    // preparereq rpc to every node
    paxos_protocol::acceptarg acc_a;
    int r;

    acc_a.v = v;
    acc_a.n = my_n;
    acc_a.instance = instance;

    handle h(*it);

    if(h.get_rpcc() != NULL) {
      // check ret val?
      h.get_rpcc()->call(paxos_protocol::acceptreq, me, acc_a, r, rpcc::to(1000));

      // if accepted
      if(r) {
        accepts.push_back(*it);
      }
    }
  }

  cout << "accept_exit" << endl;
}

void
proposer::decide(unsigned instance, std::vector<std::string> accepts, 
	      std::string v)
{
  stable = true;

  acc->commit(instance, v);
  std::cout << "COMMIT RETURNS \n " ;

  std::vector<std::string>::iterator it;

  for(it = accepts.begin(); it != accepts.end(); it++) {
    // preparereq rpc to every node
    paxos_protocol::decidearg dec_a;
    int r;

    dec_a.instance = instance;
    dec_a.v = v;

    handle h(*it);

    if(h.get_rpcc() != NULL) {
      // check ret val?
      h.get_rpcc()->call(paxos_protocol::decidereq, me, dec_a, r, rpcc::to(1000));
    }
  }

}

acceptor::acceptor(class paxos_change *_cfg, bool _first, std::string _me, 
	     std::string _value)
  : cfg(_cfg), me (_me), instance_h(0)
{
  assert (pthread_mutex_init(&pxs_mutex, NULL) == 0);

  n_h.n = 0;
  n_h.m = me;
  n_a.n = 0;
  n_a.m = me;
  v_a.clear();

  l = new log (this, me);

  if (instance_h == 0 && _first) {
    values[1] = _value;
    l->loginstance(1, _value);
    instance_h = 1;
  }

  pxs = new rpcs(atoi(_me.c_str()));
  pxs->reg(paxos_protocol::preparereq, this, &acceptor::preparereq);
  pxs->reg(paxos_protocol::acceptreq, this, &acceptor::acceptreq);
  pxs->reg(paxos_protocol::decidereq, this, &acceptor::decidereq);
}

paxos_protocol::status
acceptor::preparereq(std::string src, paxos_protocol::preparearg a,
    paxos_protocol::prepareres &r)
{
  // handle a preparereq message from proposer

  // if the proposer is lagging behind
  if(a.instance <= instance_h) {
    r.accept = false;
    r.oldinstance = a.instance;
    r.v_a = value(a.instance);
    cout << "preparereq: old ins: " << r.v_a;
  } else if(a.n > n_h) {
    r.accept = true;
    n_h = a.n;

    //log
    l->loghigh(n_h);

    r.n_a = n_a;
    r.v_a = v_a;

    cout << "prepreq: v_a: " << v_a << endl;
  }

  return paxos_protocol::OK;

}

paxos_protocol::status
acceptor::acceptreq(std::string src, paxos_protocol::acceptarg a, int &r)
{

  // handle an acceptreq message from proposer

  r = false;

  // if the proposer is lagging behind
  if(a.instance <= instance_h) {
    //
    r = false;
    cout << "acceptreq: oldins" << endl;
  } else if(a.n >= n_h) {
    n_a = a.n;
    v_a = a.v;
    cout << "updated va in acceptreq: " << v_a << endl;

    // log instance?
    l->logprop(a.n, a.v);

    r = true;
  }
  cout << "acceptreq: exit" << endl;

  return paxos_protocol::OK;
}

paxos_protocol::status
acceptor::decidereq(std::string src, paxos_protocol::decidearg a, int &r)
{

  // handle an decide message from proposer

  if(a.instance <= instance_h) {
    // actual ignore
  } else {
    commit(a.instance, a.v);
  }

  return paxos_protocol::OK;
}

void
acceptor::commit_wo(unsigned instance, std::string value)
{
  //assume pxs_mutex is held
  printf("acceptor::commit: instance=%d has v= %s\n", instance, value.c_str());
  if (instance > instance_h) {
    printf("commit: highestaccepteinstance = %d\n", instance);
    values[instance] = value;
    l->loginstance(instance, value);
    instance_h = instance;
    n_h.n = 0;
    n_h.m = me;
    n_a.n = 0;
    n_a.m = me;
    v_a.clear();
    if (cfg) {
      pthread_mutex_unlock(&pxs_mutex);
      cfg->paxos_commit(instance, value);
	std::cout << "PXS MUTEX Xq \n";
      pthread_mutex_lock(&pxs_mutex);
	std::cout << "PXS MUTEX Xp \n";

    }
  }
}

void
acceptor::commit(unsigned instance, std::string value)
{
	std::cout << "PXS MUTEX X \n";
  pthread_mutex_lock(&pxs_mutex);
	std::cout << "PXS MUTEX y \n";
  commit_wo(instance, value);
  pthread_mutex_unlock(&pxs_mutex);
}

std::string
acceptor::dump()
{
  return l->dump();
}

void
acceptor::restore(std::string s)
{
  l->restore(s);
  l->logread();
}



// For testing purposes

// Call this from your code between phases prepare and accept of proposer
void
proposer::breakpoint1()
{
  if (break1) {
    printf("Dying at breakpoint 1!\n");
    exit(1);
  }
}

// Call this from your code between phases accept and decide of proposer
void
proposer::breakpoint2()
{
  if (break2) {
    printf("Dying at breakpoint 2!\n");
    exit(1);
  }
}

void
proposer::breakpoint(int b)
{
  if (b == 3) {
    printf("Proposer: breakpoint 1\n");
    break1 = true;
  } else if (b == 4) {
    printf("Proposer: breakpoint 2\n");
    break2 = true;
  }
}
