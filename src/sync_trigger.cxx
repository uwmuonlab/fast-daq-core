#include "sync_trigger.hh"

namespace daq {

SyncTrigger::SyncTrigger() :
  ctx_(1),
  trigger_sck_(ctx_, ZMQ_PUB),
  register_sck_(ctx_, ZMQ_REP), 
  status_sck_(ctx_, ZMQ_REP),
  heartbeat_sck_(ctx_, ZMQ_SUB)
{
  DefaultInit();
  InitSockets();
  LaunchThreads();
}

SyncTrigger::SyncTrigger(std::string address) : 
  ctx_(1),
  trigger_sck_(ctx_, ZMQ_PUB),
  register_sck_(ctx_, ZMQ_REP), 
  status_sck_(ctx_, ZMQ_REP),
  heartbeat_sck_(ctx_, ZMQ_SUB)
{
  DefaultInit();

  // Get the register socket tcpip from 
  base_tcpip_ = address;

  InitSockets();
  LaunchThreads();
}

SyncTrigger::SyncTrigger(std::string address, int port) : 
  ctx_(1),
  trigger_sck_(ctx_, ZMQ_PUB),
  register_sck_(ctx_, ZMQ_REP), 
  status_sck_(ctx_, ZMQ_REP),
  heartbeat_sck_(ctx_, ZMQ_SUB)
{
  DefaultInit();

  // Get the register socket address from 
  register_address_ = ConstructAddress(address, port);

  InitSockets();
  LaunchThreads();
}

SyncTrigger::SyncTrigger(boost::property_tree::ptree &conf) :
  ctx_(1),
  trigger_sck_(ctx_, ZMQ_PUB),
  register_sck_(ctx_, ZMQ_REP), 
  status_sck_(ctx_, ZMQ_REP),
  heartbeat_sck_(ctx_, ZMQ_SUB)
{
  DefaultInit();

  // Set the register socket address from the input config file.

  base_tcpip_ = conf.get<std::string>("trigger_address", default_tcpip_); 
  base_port_ = conf.get<int>("trigger_port", default_port_);

  InitSockets();
  LaunchThreads();
}

void SyncTrigger::DefaultInit()
{
  // Set default values
  base_port_ = default_port_;
  base_tcpip_ = default_tcpip_;

  num_clients_ = 0;
  triggering_ = false;
  fix_num_clients_ = false;
  clients_good_ = true;
  thread_live_ = true;
}

void SyncTrigger::InitSockets()
{
  int port_ref = base_port_;
  int zero;

  // Now the registration socket
  register_sck_.setsockopt(ZMQ_RCVTIMEO, &timeout_, sizeof(timeout_));
  register_sck_.setsockopt(ZMQ_SNDTIMEO, &timeout_, sizeof(timeout_));
  register_address_ = ConstructAddress(base_tcpip_, port_ref++);

  try {
    register_sck_.bind(register_address_.c_str());

  } catch (zmq::error_t) {

    std::cerr << "Couldn't bind to given address." << std::endl;
    exit(EXIT_FAILURE);
  }

  // Set up the trigger socket
  trigger_sck_.setsockopt(ZMQ_RCVTIMEO, &timeout_, sizeof(timeout_));
  trigger_sck_.setsockopt(ZMQ_SNDTIMEO, &timeout_, sizeof(timeout_));

  do {
    trigger_address_ = ConstructAddress(base_tcpip_, port_ref++);

    try {
      trigger_sck_.bind(trigger_address_.c_str());
      break;

    } catch (zmq::error_t e) {

      continue;
    }
  } while (true);

  // Now the status socket
  status_sck_.setsockopt(ZMQ_RCVTIMEO, &timeout_, sizeof(timeout_));
  status_sck_.setsockopt(ZMQ_SNDTIMEO, &timeout_, sizeof(timeout_));

  do {
    status_address_ = ConstructAddress(base_tcpip_, port_ref++);

    try {
      status_sck_.bind(status_address_.c_str());
      break;

    } catch (zmq::error_t e) {

      continue;
    }
  } while (true);

  // Now the heartbeat socket
  heartbeat_sck_.setsockopt(ZMQ_RCVTIMEO, &timeout_, sizeof(timeout_));
  heartbeat_sck_.setsockopt(ZMQ_SNDTIMEO, &timeout_, sizeof(timeout_));

  do {
    heartbeat_address_ = ConstructAddress(base_tcpip_, port_ref++);

    try {
      heartbeat_sck_.bind(heartbeat_address_.c_str());
      break;

    } catch (zmq::error_t e) {

      continue;
    }
  } while (true);

  // Subscribe to all messages.
  heartbeat_sck_.setsockopt(ZMQ_SUBSCRIBE, "", 0);
}

void SyncTrigger::LaunchThreads() 
{
  trigger_thread_ = std::thread(&SyncTrigger::TriggerLoop, this); 
  register_thread_ = std::thread(&SyncTrigger::ClientLoop, this); 
}

void SyncTrigger::TriggerLoop()
{
  std::cout << "TriggerLoop launched." << std::endl;

  zmq::message_t msg;
  zmq::message_t ready_msg;
  zmq::message_t trigger_msg;

  bool rc = false;
  int clients_ready = 0;

  while (thread_live_) {

    while (triggering_) {

      if (num_clients_ == 0) {

        // Need to keep track of time zero.
        clients_ready = 0;

      } else if (clients_ready < num_clients_) {

        // Check if any more devices are ready.
        rc = status_sck_.recv(&msg, ZMQ_DONTWAIT);

        if (rc == true) {

          // Must complete the handshake.
          do {
            rc = status_sck_.send(ready_msg, ZMQ_DONTWAIT);
          } while (!rc && thread_live_ && clients_good_);

          // Increment ready devices.
          ++clients_ready;
        }

      } else if (clients_ready >= num_clients_) {

        // Send a trigger.
        rc = trigger_sck_.send(trigger_msg);

        if (rc == true) {
          clients_ready = 0;
          clients_good_ = true;
        }
      }

      std::this_thread::yield();
      light_sleep();
    }

    // Reset for the next run.
    clients_ready = 0;
    clients_good_ = true;

    std::this_thread::yield();
    heavy_sleep();
  }
}

void SyncTrigger::ClientLoop()
{
  std::cout << "ClientLoop launched." << std::endl;
  zmq::message_t msg;

  std::string addr_str = trigger_address_;
  addr_str += std::string(";");
  addr_str += status_address_;
  addr_str += std::string(";");
  addr_str += heartbeat_address_;
  addr_str += std::string(";");

  bool rc = false;
  int clients_lost = 0;
  std::string client_name;

  std::map<std::string, long> client_time;

  while (thread_live_) {

    // Poll for devices requesting registration.
    rc = register_sck_.recv(&msg, ZMQ_DONTWAIT);

    if (rc == true) {

      // Get client name.
      client_name = std::string((char *)msg.data());

      // Send back the secondary port addresses.
      zmq::message_t confirm_msg(addr_str.size());
      std::copy(addr_str.begin(), addr_str.end(), (char *)confirm_msg.data());

      // Make sure we complete the handshake.
      do {
        rc = register_sck_.send(confirm_msg, ZMQ_DONTWAIT);
      } while (!rc && thread_live_);

      // Only increment if we are sure it isn't a reconnect
      bool client_reconnect;
      if (client_time.find(client_name) == client_time.end()) {

	client_reconnect = false;

      } else {

	client_reconnect = true;
      }

      if (!fix_num_clients_ && !client_reconnect) {

        ++num_clients_;
	std::cout << "New client "  << client_name << " registered. [";
	std::cout << num_clients_ << "]" << std::endl;

      } else {
	
	std::cout << "Client " << client_name << " reconnected. [";
	std::cout << client_time.size() << "/" << num_clients_ << "]";
      }
    }

    // Monitor the heartbeat of the clients
    rc = heartbeat_sck_.recv(&msg, ZMQ_DONTWAIT);
    
    if (rc == true) {

      std::string heartbeat_msg((char *)msg.data());

      client_time[heartbeat_msg] = systime_us();
    }

    // Now check if any clients are too old
    for (auto it = client_time.cbegin(); it != client_time.cend();) {
      
      if (systime_us() - (*it).second > client_timeout_) {

	client_time.erase(it++);

        std::cout << "Dropping unresponsive client [";
        std::cout << num_clients_ << "]." << std::endl;

	if (!fix_num_clients_) {

	  num_clients_ = client_time.size();

	} else {
	  
	  clients_good_ = (num_clients_ == client_time.size());

	}

      } else {
	++it;
      }
    }
    
    std::this_thread::yield();
    light_sleep();
  }
}

} // ::daq