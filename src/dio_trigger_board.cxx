#include "dio_trigger_board.hh"

namespace daq {

DioTriggerBoard::DioTriggerBoard(std::string device, int board_addr, 
				 board_id bid, int trg_port) :
  io_board_(device, board_addr, bid), trg_port_(trg_port) {}

void DioTriggerBoard::FireTrigger(int trg_bit, int length_us)
{
  // Get the current state and only flip the trigger bit.
  u_int8_t data;
  io_board_.ReadPort(trg_port_, data);
  data &= ~(0x1 << trg_bit);

  // Start the trigger and wait the allotted pulse time.
  io_board_.WritePort(trg_port_, data);
  usleep(length_us);

  // Now turn the trigger bit back off.
  data |= 0x1 << trg_bit;
  io_board_.WritePort(trg_port_, data);
}
 
} // ::daq