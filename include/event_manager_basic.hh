#ifndef DAQ_FAST_CORE_INCLUDE_EVENT_MANAGER_BASIC_HH_
#define DAQ_FAST_CORE_INCLUDE_EVENT_MANAGER_BASIC_HH_

/*===========================================================================*\

  author: Matthias W. Smith
  email:  mwsmith2@uw.edu
  file:   event_manager_basic.hh
  
  about:  Inherits from EventManagerBase and handles the simplest event
          building.  This class simply reads from all connected digitizers
	  and aggregates synchronized events.

\*===========================================================================*/

//--- std includes --------------------------------------------------------//
#include <string>

//--- other includes ------------------------------------------------------//
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

//--- project includes ----------------------------------------------------//
#include "event_manager_base.hh"

namespace daq {

class EventManagerBasic: public EventManagerBase {

 public:
  
  //ctor 
  EventManagerBasic(std::string conf_file);
  
  //dtor
  ~EventManagerBasic();
  
  // Launches any threads needed and start collecting data.
  int BeginOfRun();

  // Rejoins threads and stops data collection.
  int EndOfRun();

  // Reserves enough memory for the event type used by this manager.
  int ResizeEventData(event_data &data);

 private:
  
  int max_event_time_;

  // Event builder loop, it collects from data workers and sends to MIDAS.
  void RunLoop();
};

} // ::daq

#endif
