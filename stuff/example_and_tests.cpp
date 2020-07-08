#include <chrono>
#include <iostream>

#include "../dt.h"

#define DOCTEST_CONFIG_IMPLEMENT
#pragma warning( push, 0 )
#include "doctest/doctest.h"
#pragma warning( pop )


TEST_CASE("get_ms_from_dt()") {
	constexpr std::chrono::microseconds t0{ 3 };
	constexpr std::chrono::microseconds t1{ 1003 };
	constexpr std::chrono::high_resolution_clock::time_point tp0(t0);
	constexpr std::chrono::high_resolution_clock::time_point tp1(t1);
	CHECK_EQ(dt::details::get_ms_from_dt(tp1, tp0), doctest::Approx(1));
}

TEST_CASE("get_median() empty") {
	std::vector<double> v0{};
	CHECK_EQ(dt::details::get_median(v0), doctest::Approx(0.0));
}

TEST_CASE("get_median() odd") {
	std::vector<double> v1{ 2.0 };
	CHECK_EQ(dt::details::get_median(v1), doctest::Approx(2.0));
	std::vector<double> v3{ 1.0, 2.0, 3.0 };
	CHECK_EQ(dt::details::get_median(v3), doctest::Approx(2.0));
}

TEST_CASE("get_median() even") {
	std::vector<double> v2{ 2.0, 4.0 };
	CHECK_EQ(dt::details::get_median(v2), doctest::Approx(3.0));
	std::vector<double> v4{ 1.0, 2.0, 3.0, 4.0 };
	CHECK_EQ(dt::details::get_median(v4), doctest::Approx(2.5));
}

TEST_CASE("get_std_dev()") {
	std::vector<double> v{72.0, 64.0, 72.0, 102.0, 65.0, 89.0, 55.0, 97.0, 78.0, 76.0};
	const double mean = dt::details::get_mean(v);
	CHECK_EQ(dt::details::get_std_dev(v, mean), doctest::Approx(14.974));
}

TEST_CASE("auto_get_digits_before_point()") {
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(99), 2);
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(10), 2);
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(-10), 2);
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(5), 1);
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(0.1), 0);
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(0.11), 0);
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(0.01), 0);

	int i = 55;
	CHECK_EQ(dt::details::printing::auto_get_digits_before_point(i), 2);
}

TEST_CASE("get_fractional_string()") {
	CHECK_EQ(dt::details::printing::get_fractional_string(1.234, 2), "23");
	CHECK_EQ(dt::details::printing::get_fractional_string(1.235, 2), "24");
	CHECK_EQ(dt::details::printing::get_fractional_string(1.235, 3), "235");
	CHECK_EQ(dt::details::printing::get_fractional_string(1.235, 1), "2");
	CHECK_EQ(dt::details::printing::get_fractional_string(1.235, 1), "2");
}

TEST_CASE("get_num_str()") {
	CHECK_EQ(dt::details::printing::get_num_str(99.5, 2, true), "+100");
	CHECK_EQ(dt::details::printing::get_num_str(99.5, 2, false), "100");
	CHECK_EQ(dt::details::printing::get_num_str(99.1, 2, true), "+99");
	CHECK_EQ(dt::details::printing::get_num_str(99.1, 3, true), "+99.1");
	CHECK_EQ(dt::details::printing::get_num_str(99.1, 4, true), "+99.10");
	CHECK_EQ(dt::details::printing::get_num_str(99.0, 4, true), "+99.00");
	CHECK_EQ(dt::details::printing::get_num_str(0.110, 3, false), "0.110");
	CHECK_EQ(dt::details::printing::get_num_str(0.111, 3, false), "0.111");
}

TEST_CASE("factory_reset()") {
	bool callback_called = false;
	auto cb = [](const dt::Results&) {};
	dt::set_done_callback(cb);
	dt::zone("one");
	dt::factory_reset();
	CHECK(dt::dt_state.zones.empty());
	CHECK_EQ(dt::dt_state.status, dt::Status::GatheringZones);
}


void accurate_sleep(const int ms) {
	// "accurate"... but better than sleep() or std::this_thread::sleep_for()
	auto t0 = std::chrono::high_resolution_clock::now();
	bool sleep = true;
	while (sleep){
		auto t1 = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
		if (elapsed.count() > (ms * 1000))
			sleep = false;
	}
}


void result_callback(const dt::Results& results) {
	for (const dt::ZoneResult& zone_result : results) {
		// ...
	}
}


int main() {
   { // run tests
      doctest::Context context;
      int res = context.run();
		printf("\n");
      if (context.shouldExit())
         return res;
   }
	
	auto t0 = std::chrono::high_resolution_clock::now();
	auto t1 = std::chrono::high_resolution_clock::now();

	const int n = 100;
	dt::set_report_out_mode(dt::ReportOutMode::ConsoleOut);
	dt::set_sample_count(10);
	dt::set_warmup_runs(3);
	dt::set_report_time_mode(dt::ReportTimeMode::Fps);
	dt::set_done_callback(result_callback);

	for (int i = 0; i < n; ++i) {
		if (i == 3)
			dt::start();
		if (dt::zone("draw background")) {
			accurate_sleep(5);
		}
      if (dt::zone("draw shadows")) {
			accurate_sleep(3);
      }
		if (dt::zone("draw bunnies")) {
			accurate_sleep(7);
		}
		t1 = std::chrono::high_resolution_clock::now();
		const double time_delta_ms = dt::details::get_ms_from_dt(t1, t0);
		t0 = t1;
		dt::slice(time_delta_ms);
	}
}
