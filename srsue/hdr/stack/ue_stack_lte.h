/*
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */
/******************************************************************************
 * File:        ue_stack.h
 * Description: L2/L3 LTE stack class.
 *****************************************************************************/

#ifndef SRSUE_UE_STACK_LTE_H
#define SRSUE_UE_STACK_LTE_H

#include <functional>
#include <pthread.h>
#include <stdarg.h>
#include <string>

#include "mac/mac.h"
#include "rrc/rrc.h"
#include "srslte/radio/radio.h"
#include "srslte/upper/pdcp.h"
#include "srslte/upper/rlc.h"
#include "upper/nas.h"
#include "upper/usim.h"
#include "srsue/hdr/testbench.h"

#include "srslte/common/buffer_pool.h"
#include "srslte/common/log_filter.h"
#include "srslte/common/multiqueue.h"
#include "srslte/common/thread_pool.h"
#include "srslte/interfaces/ue_interfaces.h"

#include "srslte/common/time_prof.h"
#include "srsue/hdr/ue_metrics_interface.h"
#include "ue_stack_base.h"

namespace srsue {

class ue_stack_lte final : public ue_stack_base,
                           public stack_interface_phy_lte,
                           public stack_interface_gw,
                           public stack_interface_rrc,
                           public srslte::thread
{
public:
  ue_stack_lte();
  ~ue_stack_lte();

  std::string get_type() final;

  int  init(const stack_args_t& args_, srslte::logger* logger_);
  int  init(const stack_args_t& args_, srslte::logger* logger_, phy_interface_stack_lte* phy_, gw_interface_stack* gw_, testbench* tb_);
  void enable_pcap(std::string mac_filename, std::string nas_filename);
  bool switch_on() final;
  bool switch_off() final;
  bool enable_data();
  bool disable_data();
  void stop() final;
  void reset() final;
  void enable_sec_algo(sec_algo_type_t type, uint index, bool enable);

  bool get_metrics(stack_metrics_t* metrics) final;
  bool is_rrc_connected();

  // RRC interface for PHY
  void in_sync() final;
  void out_of_sync() final;
  void new_cell_meas(const std::vector<phy_meas_t>& meas) override { rrc.new_cell_meas(meas); }

  // MAC Interface for PHY
  uint16_t get_dl_sched_rnti(uint32_t tti) final { return mac.get_dl_sched_rnti(tti); }
  uint16_t get_ul_sched_rnti(uint32_t tti) final { return mac.get_ul_sched_rnti(tti); }

  void new_grant_ul(uint32_t cc_idx, mac_grant_ul_t grant, tb_action_ul_t* action) final
  {
    mac.new_grant_ul(cc_idx, grant, action);
  }

  void new_grant_dl(uint32_t cc_idx, mac_grant_dl_t grant, tb_action_dl_t* action) final
  {
    mac.new_grant_dl(cc_idx, grant, action);
  }

  void tb_decoded(uint32_t cc_idx, mac_grant_dl_t grant, bool ack[SRSLTE_MAX_CODEWORDS]) final
  {
    mac.tb_decoded(cc_idx, grant, ack);
  }

  void bch_decoded_ok(uint32_t cc_idx, uint8_t* payload, uint32_t len) final
  {
    mac.bch_decoded_ok(cc_idx, payload, len);
  }

  void mch_decoded(uint32_t len, bool crc) final { mac.mch_decoded(len, crc); }

  void new_mch_dl(const srslte_pdsch_grant_t& phy_grant, tb_action_dl_t* action) final
  {
    mac.new_mch_dl(phy_grant, action);
  }

  void set_mbsfn_config(uint32_t nof_mbsfn_services) final { mac.set_mbsfn_config(nof_mbsfn_services); }

  void run_tti(uint32_t tti, uint32_t tti_jump) final;

  // Interface for GW
  void write_sdu(uint32_t lcid, srslte::unique_byte_buffer_t sdu, bool blocking) final;

  bool is_lcid_enabled(uint32_t lcid) final { return pdcp.is_lcid_enabled(lcid); }

  // Interface for RRC
  void      start_cell_search() final;
  void      start_cell_select(const phy_interface_rrc_lte::phy_cell_t* cell) final;
  tti_point get_current_tti() final { return current_tti; }

  // Task Handling interface
  srslte::timer_handler::unique_timer    get_unique_timer() final { return timers.get_unique_timer(); }
  srslte::task_multiqueue::queue_handler make_task_queue() final { return pending_tasks.get_queue_handler(); }
  void                                   enqueue_background_task(std::function<void(uint32_t)> f) final;
  void                                   notify_background_task_result(srslte::move_task_t task) final;
  void                                   defer_callback(uint32_t duration_ms, std::function<void()> func) final;
  void                                   defer_task(srslte::move_task_t task) final;

private:
  void run_thread() final;
  void run_tti_impl(uint32_t tti, uint32_t tti_jump);
  void stop_impl();

  const uint32_t                  TTI_STAT_PERIOD = 1024;
  const std::chrono::milliseconds TTI_WARN_THRESHOLD_MS{5};
  const uint32_t                  SYNC_QUEUE_WARN_THRESHOLD = 5;

  bool                running;
  srsue::stack_args_t args;

  srslte::tti_point current_tti;

  // timers
  srslte::timer_handler timers;

  // UE stack logging
  srslte::logger* logger = nullptr;
  srslte::log_ref stack_log{"STCK"}; ///< our own log filter
  srslte::log_ref mac_log{"MAC"};
  srslte::log_ref rlc_log{"RLC"};
  srslte::log_ref pdcp_log{"PDCP"};
  srslte::log_ref rrc_log{"RRC"};
  srslte::log_ref usim_log{"USIM"};
  srslte::log_ref nas_log{"NAS"};
  srslte::log_ref pool_log{"POOL"};

  // RAT-specific interfaces
  phy_interface_stack_lte* phy = nullptr;
  gw_interface_stack*      gw  = nullptr;

  // Thread
  static const int        STACK_MAIN_THREAD_PRIO = -1; // Use default high-priority below UHD
  srslte::task_multiqueue pending_tasks;
  int sync_queue_id = -1, ue_queue_id = -1, gw_queue_id = -1, stack_queue_id = -1, background_queue_id = -1;
  srslte::task_thread_pool             background_tasks;     ///< Thread pool used for long, low-priority tasks
  std::vector<srslte::move_task_t>     deferred_stack_tasks; ///< enqueues stack tasks from within. Avoids locking
  srslte::block_queue<stack_metrics_t> pending_stack_metrics;

  // TTI stats
  srslte::tprof<srslte::sliding_window_stats_ms> tti_tprof;

  // stack components
  srsue::mac                 mac;
  srslte::mac_pcap           mac_pcap;
  srslte::nas_pcap           nas_pcap;
  srslte::rlc                rlc;
  srslte::pdcp               pdcp;
  srsue::rrc                 rrc;
  srsue::nas                 nas;
  std::unique_ptr<usim_base> usim;

  testbench*               tb  = nullptr;
};

} // namespace srsue

#endif // SRSUE_UE_STACK_LTE_H
