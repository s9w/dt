#pragma once

#include <algorithm> // for std::sort(), std::max()
#include <cmath>
#include <string>
#include <vector>

#ifndef DT_NO_CHRONO
#include <chrono>
#endif // DT_NO_CHRONO

namespace dt {
#ifdef DT_FLOATS
   using float_type = float;
#else
   using float_type = double;
#endif

   struct ZoneResult {
      std::string name;
      std::vector<float_type> sorted_times;
      float_type median;
      float_type mean;
      float_type worst_time;
      float_type std_dev;
   };

   using Results = std::vector<ZoneResult>;
   inline Results results;
   inline std::string result_str;

   enum class Status { GatheringZones, Ready, Starting, Measuring, Evaluating };
   enum class ReportOutMode { JustEval, ConsoleOut };
   enum class ReportTimeMode { Ms, Fps };

   struct Zone {
      std::string name;
      std::vector<float_type> frame_times;
   };

   inline struct State {
      Status status = Status::GatheringZones;
      std::vector<Zone> zones;
      size_t target_zone = 0;
      std::chrono::high_resolution_clock::time_point t0;
      int recorded_slices = 0;
      int warmup_runs_left = 0;
   } dt_state;

   typedef void (*DoneCallback)(const Results& results);

   inline struct Config {
      ReportOutMode report_out_mode = ReportOutMode::ConsoleOut;
      ReportTimeMode report_time_mode = ReportTimeMode::Ms;
      int target_sample_count = 100;
      int warmup_runs = 10;
      DoneCallback done_cb = nullptr;
   } config;


   inline bool zone(const std::string& zone_name);
   inline void start();
   inline void slice(const float_type time_delta_ms);
#ifndef DT_NO_CHRONO
   inline void slice();
#endif // DT_NO_CHRONO

   inline auto set_sample_count(const int sample_count) -> void;
   inline auto set_warmup_runs(const int warmup_runs) -> void;
   inline auto set_report_out_mode(const ReportOutMode report_out_mode) -> void;
   inline auto set_report_time_mode(const ReportTimeMode report_time_mode) -> void;
   inline auto set_done_callback(DoneCallback cb) -> void;
   inline auto are_results_ready() -> bool;
   inline auto clear_results() -> void;
   inline auto factory_reset() -> void;

} // namespace dt


namespace dt::details {

   [[nodiscard]] constexpr auto get_ms_from_dt(
      const std::chrono::high_resolution_clock::time_point& t1,
      const std::chrono::high_resolution_clock::time_point& t0
   ) -> float_type {
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
      return ns / static_cast<float_type>(1'000'000.0);
   }


   [[nodiscard]] inline auto get_zone_index(
      const std::string& zone_name,
      const State& state
   ) -> std::ptrdiff_t {
      const auto it = std::find_if(
         std::cbegin(state.zones),
         std::cend(state.zones),
         [zone_name](const Zone& zone) {
            return zone.name == zone_name;
         }
      );
      if (it == std::cend(state.zones))
         return -1;
      const std::ptrdiff_t index = std::distance(std::cbegin(state.zones), it);
      return index;
   }


   [[nodiscard]] inline auto is_zone_known(
      const std::string& zone_name,
      const State& state
   ) -> bool {
      return get_zone_index(zone_name, state) != -1;
   }


   [[nodiscard]] constexpr auto is_sample_target_reached(
      const State& state,
      const Config& pconfig
   ) -> bool {
      return state.recorded_slices >= pconfig.target_sample_count;
   }


   [[nodiscard]] inline auto are_all_zones_done(const State& state) -> bool {
      return state.target_zone >= state.zones.size();
   }


   // doesn't touch zone names, status or t0
   [[nodiscard]] inline auto reset_state(State& state) -> void {
      state.target_zone = 0;
      state.recorded_slices = 0;
      state.warmup_runs_left = config.warmup_runs;
      for (Zone& zone : state.zones)
         zone.frame_times.clear();
   }


   // fun fact: median means mean of middle elements for even sizes
   [[nodiscard]] inline auto get_median(std::vector<float_type>& sorted_vec) -> float_type {
      if (sorted_vec.empty()) // ask stupid questions, get stupid answers
         return 0.0;
      if (sorted_vec.size() % 2 == 0) { // even size
         const size_t i_middle_left = sorted_vec.size() / 2 - 1;
         return static_cast<float_type>(0.5)* (sorted_vec[i_middle_left] + sorted_vec[i_middle_left + 1]);
      }
      else { // odd size
         const size_t i_middle = sorted_vec.size() / 2;
         return sorted_vec[i_middle];
      }
   }


   // std::accumulate would require <numeric>
   [[nodiscard]] inline auto get_mean(std::vector<float_type>& vec) -> float_type {
      float_type sum = 0.0;
      for (const float_type value : vec)
         sum += value;
      return sum / vec.size();
   }


   // This is bessel-corrected!
   [[nodiscard]] inline auto get_std_dev(
      std::vector<float_type>& vec,
      const float_type mean
   ) -> float_type {
      float_type squares = 0.0;
      for (const float_type value : vec) {
         const float_type term = value - mean;
         squares += term * term;
      }
      return std::sqrt(squares / (vec.size() - static_cast<float_type>(1.0)));
   }


   [[nodiscard]] inline auto get_results(const std::vector<Zone>& zones) -> Results {
      Results local_results;
      for (const Zone& zone : zones) {
         ZoneResult zr;
         zr.name = zone.name;
         zr.sorted_times = zone.frame_times;
         std::sort(std::begin(zr.sorted_times), std::end(zr.sorted_times));
         zr.median = get_median(zr.sorted_times);
         zr.mean = get_mean(zr.sorted_times);
         zr.std_dev = get_std_dev(zr.sorted_times, zr.mean);
         zr.worst_time = zr.sorted_times.back();
         local_results.emplace_back(zr);
      }
      return local_results;
   }


   inline auto record_slice(State& state, const float_type time_delta_ms) -> void {
      state.zones[state.target_zone].frame_times.emplace_back(time_delta_ms);
      ++state.recorded_slices;
   }


   inline auto start_next_zone_measurement(State& state) -> void {
      ++state.target_zone;
      state.recorded_slices = 0;
   }

   namespace printing {

      [[nodiscard]] auto inline get_max_zone_name_len(const Results& lresults, const int min_len) -> int {
         int max_name_len = min_len;
         for (const ZoneResult& result : lresults) {
            const int len = static_cast<int>(result.name.length());
            if (len > max_name_len)
               max_name_len = len;
         }
         return max_name_len;
      }


      enum class EvalType { Median, Mean, Worst, StdDev };


      [[nodiscard]] constexpr auto get_result_eval(
         const ZoneResult& result,
         const EvalType eval_type,
         const ReportTimeMode time_mode
      ) -> float_type {
         if (eval_type == EvalType::StdDev)
            return result.std_dev;
         float_type ms_value = static_cast<float_type>(0.0);
         if (eval_type == EvalType::Median)
            ms_value = result.median;
         else if (eval_type == EvalType::Mean)
            ms_value = result.mean;
         else if (eval_type == EvalType::Worst)
            ms_value = result.worst_time;

         if (time_mode == ReportTimeMode::Ms)
            return ms_value;
         else
            return static_cast<float_type>(1000.0) / ms_value;
      }


      template<class T>
      [[nodiscard]] constexpr auto auto_get_digits_before_point(const T num) -> int {
         if (std::abs(num) < static_cast<float_type>(1.0))
            return 0;
         return std::max(static_cast<int>(std::log10(std::abs(num))) + 1, 0);
      }


      [[nodiscard]] inline auto get_fractional_string(
         const float_type num,
         const int digits
      ) -> std::string {
         float_type integral;
         const float_type fractional = std::modf(num, &integral);
         const float_type x = fractional * std::pow(static_cast<float_type>(10.0), digits);
         const int i = static_cast<int>(std::round(x));
         std::string s = std::to_string(i);
         s.resize(digits, '0');
         return s;
      }


      [[nodiscard]] inline auto get_num_str(
         const float_type num,
         const int significant_digits,
         const bool with_sign
      ) -> std::string {
         std::string s;
         if (with_sign)
            s += num < static_cast<float_type>(0.0) ? "-" : "+";
         const float_type abs_num = std::abs(num);

         {
            const int whole_rounded = static_cast<int>(std::round(abs_num));
            const int rounded_predot_digits = auto_get_digits_before_point(whole_rounded);
            if (rounded_predot_digits >= significant_digits) {
               s += std::to_string(whole_rounded);
               return s;
            }
         }

         const int predot_digits = auto_get_digits_before_point(abs_num);
         s += std::to_string(static_cast<int>(abs_num));

         const int digits_left = significant_digits - predot_digits;
         if (digits_left > 0) {
            s += ".";
            s += get_fractional_string(abs_num, digits_left);
         }

         return s;
      }


      [[nodiscard]] constexpr auto get_percentage(
         const float_type numerator,
         const float_type denominator
      ) -> float_type {
         return static_cast<float_type>(100.0)* numerator / denominator;
      }


      [[nodiscard]] inline auto get_cell_str(
         const Results& lresults,
         const int i,
         const EvalType& eval_type,
         const ReportTimeMode time_mode
      ) -> std::string {
         const float_type value = get_result_eval(lresults[i], eval_type, time_mode);
         if (eval_type == EvalType::StdDev)
            return get_num_str(get_percentage(value, lresults[i].mean), 3, false);
         std::string cell_str = get_num_str(value, 3, false);
         if (i == 0)
            return cell_str;

         const float_type baseline = get_result_eval(lresults[0], eval_type, time_mode);
         const float_type diff = value - baseline;
         const float_type improv_percent = get_percentage(diff, baseline);
         cell_str += " (" + get_num_str(improv_percent, 2, true) + "%)";
         return cell_str;
      }


      struct ResultTable {
         std::vector<std::string> median_cells;
         std::vector<std::string> mean_cells;
         std::vector<std::string> worst_cells;
         std::vector<std::string> std_dev_cells;
         int max_median_len = 3;
         int max_mean_len = 3;
         int max_worst_len = 3;
         int max_stddev_len = 3;
      };


      [[nodiscard]] inline auto get_result_table(
         const Results& lresults,
         const ReportTimeMode time_mode
      ) -> ResultTable {
         ResultTable table;
         for (int i = 0; i < lresults.size(); ++i) {
            const std::string median_cell = get_cell_str(lresults, i, EvalType::Median, time_mode);
            table.median_cells.emplace_back(median_cell);
            table.max_median_len = std::max(table.max_median_len, static_cast<int>(median_cell.length()));

            const std::string mean_cell = get_cell_str(lresults, i, EvalType::Mean, time_mode);
            table.mean_cells.emplace_back(mean_cell);
            table.max_mean_len = std::max(table.max_mean_len, static_cast<int>(mean_cell.length()));

            const std::string worst_cell = get_cell_str(lresults, i, EvalType::Worst, time_mode);
            table.worst_cells.emplace_back(worst_cell);
            table.max_worst_len = std::max(table.max_worst_len, static_cast<int>(worst_cell.length()));

            const std::string rel_std_dev_cell = get_cell_str(lresults, i, EvalType::StdDev, time_mode);
            table.std_dev_cells.emplace_back(rel_std_dev_cell);
            table.max_stddev_len = std::max(table.max_stddev_len, static_cast<int>(rel_std_dev_cell.length()));
         }
         return table;
      }


      inline auto get_united_str(
         const std::string& description,
         const Config& pconfig
      ) -> std::string {
         std::string s = description;
         if (pconfig.report_time_mode == ReportTimeMode::Fps)
            s += "[fps]";
         else
            s += "[ms]";
         return s;
      }


      inline auto header_print(
         char* buffer,
         const size_t buffer_len,
         const int name_col_len,
         const ResultTable& table,
         const Config& pconfig
      ) -> int {
         return snprintf(
            buffer,
            buffer_len,
            "%*s %-*s %-*s %-*s %-*s\n",
            name_col_len, "",
            table.max_median_len, get_united_str("median", pconfig).c_str(),
            table.max_mean_len, get_united_str("mean", pconfig).c_str(),
            table.max_worst_len, get_united_str("worst", pconfig).c_str(),
            table.max_stddev_len, "std dev[%]"
         );
      }


      inline auto row_print(
         char* buffer,
         const size_t buffer_len,
         const int name_col_len,
         const std::string& name_col,
         const ResultTable& table,
         const int i
      ) -> int {
         return snprintf(
            buffer,
            buffer_len,
            "%-*s %-*s %-*s %-*s %-*s\n",
            name_col_len,
            name_col.c_str(),
            table.max_median_len, table.median_cells[i].c_str(),
            table.max_mean_len, table.mean_cells[i].c_str(),
            table.max_worst_len, table.worst_cells[i].c_str(),
            table.max_stddev_len, table.std_dev_cells[i].c_str()
         );
      }


      inline auto get_result_str(
         const Results& lresults,
         const Config& pconfig
      ) -> std::string {
         const char* wo_prefix = "w/o ";
         int name_col_len = get_max_zone_name_len(lresults, 3);
         name_col_len += static_cast<int>(strlen(wo_prefix));
         name_col_len += 1; // for colon
         constexpr int decimal_places = 1;
         const ResultTable table = get_result_table(lresults, pconfig.report_time_mode);

         std::string output_str;
         {
            const int header_buf_len = header_print(nullptr, 0, name_col_len, table, pconfig);
            output_str.resize(header_buf_len + 1);
         }

         header_print(&output_str.front(), output_str.size(), name_col_len, table, pconfig);
         output_str.pop_back(); // remove null terminator

         for (int i = 0; i < lresults.size(); ++i) {
            const ZoneResult& result = lresults[i];
            std::string name_col = "all";
            if (i != 0)
               name_col = wo_prefix + result.name;
            name_col += ":";
            {
               const int row_size = row_print(nullptr, 0, name_col_len, name_col, table, i);
               const size_t old_size = output_str.size();
               output_str.resize(old_size + row_size + 1);
               row_print(&output_str[old_size], row_size + 1, name_col_len, name_col, table, i);
            }
            output_str.pop_back(); // remove null terminator
         }
         output_str.push_back('\0');
         return output_str;
      }

   } // namespace printing

} // namespace dt::details


inline bool dt::zone(const std::string& zone_name) {
   if (dt_state.status == Status::GatheringZones) {
      if (dt_state.zones.empty()) { // init the 'null zone' 
         dt_state.zones.emplace_back();
         dt_state.zones.back().frame_times.reserve(config.target_sample_count);
      }

      // Encountering the same zone name twice means things can start
      if (details::is_zone_known(zone_name, dt_state)) {
         dt_state.status = Status::Ready;
      }
      else {
         dt_state.zones.push_back({ zone_name, {} });
         dt_state.zones.back().frame_times.reserve(config.target_sample_count);
      }
   }
   else if (dt_state.status == Status::Measuring) {
      const size_t current_zone = details::get_zone_index(zone_name, dt_state);
      if (dt_state.target_zone == 0)
         return true;
      return current_zone != dt_state.target_zone;
   }
   return true;
}


inline void dt::start() {
   if (dt_state.status != Status::Ready) {
      return;
   }
   dt_state.status = Status::Starting; // Wait until the next slice for timing
}


inline void dt::slice(const float_type time_delta_ms) {
   if (dt_state.status == Status::GatheringZones || dt_state.status == Status::Ready) {
      return;
   }
   else if (dt_state.status == Status::Starting) {
      details::reset_state(dt_state);
      dt_state.status = Status::Measuring;
   }
   else if (dt_state.status == Status::Measuring) {
      if (dt_state.warmup_runs_left > 0) {
         --dt_state.warmup_runs_left;
         return;
      }
      details::record_slice(dt_state, time_delta_ms);
      if (details::is_sample_target_reached(dt_state, config)) {
         details::start_next_zone_measurement(dt_state);
         if (details::are_all_zones_done(dt_state))
            dt_state.status = Status::Evaluating;
      }
   }
   else if (dt_state.status == Status::Evaluating) {
      results = details::get_results(dt_state.zones);
      result_str = details::printing::get_result_str(results, config);
      if (config.report_out_mode == ReportOutMode::ConsoleOut)
         printf("%s", result_str.c_str());
      if (config.done_cb != nullptr)
         config.done_cb(results);
      dt_state.status = Status::Ready;
   }
}



#ifndef DT_NO_CHRONO
inline void dt::slice() {
   float_type time_delta_ms = 0.0;
   if (dt_state.status == Status::Starting) {
      dt_state.t0 = std::chrono::high_resolution_clock::now();
   }
   else if (dt_state.status == Status::Measuring) {
      const auto t1 = std::chrono::high_resolution_clock::now();
      time_delta_ms = details::get_ms_from_dt(t1, dt_state.t0);
      dt_state.t0 = t1;
   }
   slice(time_delta_ms);
}
#endif // DT_NO_CHRONO


inline auto dt::set_sample_count(const int sample_count) -> void {
   dt::config.target_sample_count = sample_count;
}


inline auto dt::set_warmup_runs(const int warmup_runs) -> void {
   dt::config.warmup_runs = warmup_runs;
}


inline auto dt::set_report_out_mode(const ReportOutMode report_out_mode) -> void {
   dt::config.report_out_mode = report_out_mode;
}


inline auto dt::set_report_time_mode(const ReportTimeMode report_time_mode) -> void {
   dt::config.report_time_mode = report_time_mode;
}


inline auto dt::set_done_callback(DoneCallback cb) -> void {
   config.done_cb = cb;
}


inline auto dt::are_results_ready() -> bool {
   return dt_state.status == Status::Ready && dt_state.recorded_slices > 0;
}


inline auto dt::clear_results() -> void {
   results.clear();
   result_str.clear();
}


inline auto dt::factory_reset() -> void {
   dt_state.zones.clear();
   dt_state.status = Status::GatheringZones;
   details::reset_state(dt_state);
}
