#pragma once

#include <algorithm> // for std::sort(), std::max()
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

	struct ZoneResult;

	struct ZoneResult {
		std::string name;
		std::vector<float_type> sorted_times;
		float_type median;
		float_type average;
		float_type max_time;
		float_type std_dev;
	};

	using Results = std::vector<ZoneResult>;

	Results results;

	enum class Status { GatheringZones, Ready, Starting, Measuring, Evaluating };
	enum class ReportMode { JustEval, ConsoleOut };

	struct Zone {
		std::string name;
		std::vector<float_type> frame_times;
	};

	struct State {
		Status status = Status::GatheringZones;
		std::vector<Zone> zones;
		size_t target_zone = 0;
		std::chrono::high_resolution_clock::time_point t0;
		int recorded_slices = 0;
	} dt_state;

	typedef void (*DoneCallback)(const Results& results);
	//typedef void (*GLFWframebuffersizefun)(GLFWwindow*, int, int);

	struct Config {
		ReportMode report_mode = ReportMode::ConsoleOut;
		int target_sample_count = 10;
		DoneCallback done_cb = nullptr;

	} config;

	namespace details {

		[[nodiscard]] constexpr auto get_ms_from_dt(
			const std::chrono::high_resolution_clock::time_point& t1,
			const std::chrono::high_resolution_clock::time_point& t0
		) -> float_type {
			const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
			return ns / static_cast<float_type>(1'000'000.0);
		}


		[[nodiscard]] auto get_zone_index(
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


      [[nodiscard]] auto is_zone_known(
         const std::string& zone_name,
         const State& state
      ) -> bool {
			return get_zone_index(zone_name, state) != -1;
      }


		[[nodiscard]] constexpr auto is_sample_target_reached(const State& state) -> bool {
			return state.recorded_slices >= config.target_sample_count;
		}


      [[nodiscard]] auto are_all_zones_done(const State& state) -> bool {
			return state.target_zone >= state.zones.size();
      }


		// fun fact: median means average of middle elements for even sizes
      [[nodiscard]] auto get_median(std::vector<float_type>& sorted_vec) -> float_type {
         if (sorted_vec.empty()) // ask stupid questions, get stupid answers
            return 0.0;
         if (sorted_vec.size() % 2 == 0) { // even size
            const size_t i_middle_left = sorted_vec.size() / 2 - 1;
            return static_cast<float_type>(0.5) * (sorted_vec[i_middle_left] + sorted_vec[i_middle_left + 1]);
         }
         else { // odd size
            const size_t i_middle = sorted_vec.size() / 2;
            return sorted_vec[i_middle];
         }
      }


		/// std::accumulate would require <numeric>
      [[nodiscard]] auto get_average(std::vector<float_type>& vec) -> float_type {
			float_type sum = 0.0;
			for (const float_type value : vec)
				sum += value;
			return sum / vec.size();
      }


		// This is bessel-corrected!
		[[nodiscard]] auto get_std_dev(
			std::vector<float_type>& vec,
			const float_type mean
		) -> float_type{
			float_type sigma = 0.0;
			for (const float_type value : vec) {
				const float_type term = value - mean;
				sigma += term * term;
			}
			return sqrt(sigma / (vec.size() - static_cast<float_type>(1.0)));
		}


		[[nodiscard]] auto get_results(const std::vector<Zone>& zones) -> Results {
			Results local_results;
			for(const Zone& zone : zones){
				ZoneResult zr;
				zr.name = zone.name;
				zr.sorted_times = zone.frame_times;
				std::sort(std::begin(zr.sorted_times), std::end(zr.sorted_times));
				zr.median = get_median(zr.sorted_times);
				zr.average = get_average(zr.sorted_times);
				zr.std_dev = get_std_dev(zr.sorted_times, zr.average);
				zr.max_time = zr.sorted_times.back();
				local_results.emplace_back(zr);
			}
			return local_results;
		}


      auto record_slice(State& state, const float_type time_delta_ms) -> void {
         state.zones[state.target_zone].frame_times.emplace_back(time_delta_ms);
         ++state.recorded_slices;
      }


      auto start_next_zone_measurement(State& state) -> void {
         ++state.target_zone;
			state.recorded_slices = 0;
      }

		namespace printing {

			[[nodiscard]] auto get_max_zone_name_len(const Results& lresults, const int min_len) -> int {
				int max_name_len = min_len;
				for (const ZoneResult& result : lresults) {
					const int len = static_cast<int>(result.name.length());
					if (len > max_name_len)
						max_name_len = len;
				}
				return max_name_len;
			}


			enum class EvalType { Median, Average, Max, RelStdDev };


			[[nodiscard]] auto get_result_eval(
				const ZoneResult& result,
				const EvalType& eval_type
			) -> float_type {
				switch (eval_type) {
				case EvalType::Median:
					return result.median;
					break;
				case EvalType::Average:
					return result.average;
					break;
				case EvalType::Max:
					return result.max_time;
					break;
				case EvalType::RelStdDev:
					return result.std_dev / result.average;
					break;
				default:
					return 0.0;
				}
			}


			[[nodiscard]] auto get_cell_str(
				const Results& lresults,
				const int i,
				const EvalType& eval_type
			) -> std::string {
				char value_buffer[50];
				if (eval_type == EvalType::RelStdDev) {
					sprintf_s(value_buffer, "%5.1f", static_cast<float_type>(100.0) * get_result_eval(lresults[i], eval_type));
					return std::string(value_buffer);
				}
				char change_buffer[50];
				sprintf_s(value_buffer, "%5.1f", get_result_eval(lresults[i], eval_type));
				if (i != 0) {
					const float_type baseline = get_result_eval(lresults[0], eval_type);
					const float_type diff = get_result_eval(lresults[i], eval_type) - baseline;
					const float_type improv_percent = static_cast<float_type>(100.0) * diff / baseline;
					sprintf_s(change_buffer, "(%+4.1f%%)", improv_percent);
				}
            std::string str(value_buffer);
            str += " ";
				if (i != 0)
					str += change_buffer;
				return str;
			}


			struct ResultTable {
				std::vector<std::string> median_cells;
				std::vector<std::string> average_cells;
				std::vector<std::string> max_cells;
				std::vector<std::string> std_dev_cells;
				int max_median_len = 3;
				int max_avg_len = 3;
				int max_max_len = 3;
				int max_stddev_len = 3;
			};


			[[nodiscard]] auto get_result_table(const Results& lresults) -> ResultTable {
				ResultTable table;
				for (int i = 0; i < lresults.size(); ++i) {
					const std::string median_cell = get_cell_str(lresults, i, EvalType::Median);
					table.median_cells.emplace_back(median_cell);
					table.max_median_len = std::max(table.max_median_len, static_cast<int>(median_cell.length()));

					const std::string avg_cell = get_cell_str(lresults, i, EvalType::Average);
					table.average_cells.emplace_back(avg_cell);
					table.max_avg_len = std::max(table.max_avg_len, static_cast<int>(avg_cell.length()));

					const std::string max_cell = get_cell_str(lresults, i, EvalType::Max);
					table.max_cells.emplace_back(max_cell);
					table.max_max_len = std::max(table.max_max_len, static_cast<int>(max_cell.length()));

					const std::string rel_std_dev_cell = get_cell_str(lresults, i, EvalType::RelStdDev);
					table.std_dev_cells.emplace_back(rel_std_dev_cell);
					table.max_stddev_len = std::max(table.max_stddev_len, static_cast<int>(rel_std_dev_cell.length()));
				}
				return table;
			}


			auto print_results(const Results& lresults) -> void {
				const char* wo_prefix = "w/o ";
				int name_col_len = get_max_zone_name_len(lresults, 3);
				name_col_len += static_cast<int>(strlen(wo_prefix));
				constexpr int decimal_places = 1;
				const ResultTable table = get_result_table(lresults);

				printf("%*s  %-*s %-*s %-*s %-*s\n",
					name_col_len, "",
					table.max_median_len, "median [ms]",
					table.max_avg_len, "average [ms]",
					table.max_max_len, "max [ms]",
					table.max_stddev_len, "std dev [%]"
				);
				for (int i = 0; i < lresults.size(); ++i) {
					const ZoneResult& result = lresults[i];
					std::string name_col = "all";
					if (i != 0)
						name_col = wo_prefix + result.name;
					printf("%-*s:", name_col_len, name_col.c_str());
					printf(" %-*s", table.max_median_len, table.median_cells[i].c_str());
					printf(" %-*s", table.max_avg_len, table.average_cells[i].c_str());
					printf(" %-*s", table.max_max_len, table.max_cells[i].c_str());
					printf(" %-*s", table.max_stddev_len, table.std_dev_cells[i].c_str());
					printf("\n");
				}
			}

		} // namespace printing

	} // namespace details


   void slice(const float_type time_delta_ms) {
      if (dt_state.status == Status::GatheringZones || dt_state.status == Status::Ready) {
         return;
      }
      else if (dt_state.status == Status::Starting) {
         dt_state.target_zone = 0;
         dt_state.recorded_slices = 0;
			dt_state.zones[0].frame_times.clear();
         dt_state.status = Status::Measuring;
      }
      else if (dt_state.status == Status::Measuring) {
         details::record_slice(dt_state, time_delta_ms);
         if (details::is_sample_target_reached(dt_state)) {
            details::start_next_zone_measurement(dt_state);
            if (details::are_all_zones_done(dt_state))
               dt_state.status = Status::Evaluating;
         }
      }
      else if (dt_state.status == Status::Evaluating) {
         results = details::get_results(dt_state.zones);
			if(config.report_mode == ReportMode::ConsoleOut)
				details::printing::print_results(results);
			if(config.done_cb != nullptr)
				config.done_cb(results);
         dt_state.status = Status::Ready;
      }
   }


#ifndef DT_NO_CHRONO
	void slice() {
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


	bool zone(const std::string& zone_name) {
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


	auto set_sample_count(const int sample_count) -> void {
		config.target_sample_count = sample_count;
	}


   auto set_report_mode(const ReportMode report_mode) -> void {
		config.report_mode = report_mode;
   }


   auto set_done_callback(DoneCallback cb) -> void {
		config.done_cb = cb;
   }


	auto are_results_ready() -> bool {
		return dt_state.status == Status::Ready && dt_state.recorded_slices > 0;
	}


	void start() {
		if (dt_state.status != Status::Ready) {
			return;
		}
		dt_state.status = Status::Starting; // Wait until the next slice for timing
	}

}
