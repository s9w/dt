# differential timer (dt)
`dt` is a small C++ single-file library for performance measurements.

Profiling c++ code is usually done with either sampling profilers or frame profilers like the magnificent [Tracy](https://github.com/wolfpld/tracy). They measure the time it takes to run parts of the code. `dt` works by skipping certain parts of the code and measuring the resulting time difference.

Those two things are often not identical because of cache and other effects. Also this method can accurately measure the performance impact of drawing commands on the GPU with OpenGL and other drawing libraries. There you have draw commands which are scheduled by the driver to execute whenever it considers best. There are [means](https://www.khronos.org/opengl/wiki/Query_Object#Timer_queries) to profile draw calls directly, but I often got unreliable results from those.

The drawback is that this is only possible if the code is still runnable in a reasonable state if parts are skipped. That is usually feasible in realtime applications where old data produce a realistic (although not up-to-date) workload.

## Usage
There is only the `dt.h` header file. C++17 is required. Basic usage:

```c++
#include "dt.h"

int main() {
   dt::set_sample_count(10);
   dt::set_warmup_runs(3);
   for (int i = 0; i < 100; ++i) {
      if (i == 1)
         dt::start();

      if (dt::zone("draw backgroud"))
         // do something for 5 ms
      if (dt::zone("draw shadows"))
         // do something for 3 ms
      if (dt::zone("draw bunnies"))
         // do something for 7 ms
      dt::slice();
   }
}
```
Something like this would print
```
                     median[ms]  mean[ms]    worst[ms]   std dev[%]
all:                 15.0        15.0        15.1        0.106
w/o draw background: 10.0 (-33%) 10.0 (-33%) 10.0 (-33%) 0.760
w/o draw shadows:    12.0 (-20%) 12.0 (-20%) 12.1 (-20%) 0.730
w/o draw bunnies:    8.30 (-47%) 8.40 (-47%) 8.90 (-46%) 0.280
```
So you fence the code you want to toggle with `if(dt::zone())` conditionals, tell it where the frame ends with `dt::slice()` and start.

When `dt::start()` is called, it waits until the next `dt::slice()`, which should usually be directly before or after your `SwapBuffer()`. It will then measure baseline i.e. the runtime with all zones enabled for the number of *slices* specified by `dt::set_sample_count(int)` (default is 100). After that it will run without the first zone, then without the second and so on. When a new zone configuration is started, `dt` does warmup runs before the timing is recorded. The number can be set with `dt::set_warmup_runs(int)` and is 10 by default.

## Results
By default, the results are printed to the console via `printf()`. The console output can be disabled with `dt::set_report_mode(dt::ReportMode::JustEval)`. Either way the result string is stored in a `std::string` in `dt::result_str`. Feel free to take that and print it in `cout`, your favorite logging library, file output etc. Instead of frame times you can also output frames per second with `dt::set_report_time_mode(dt::ReportTimeMode::Fps);`. That will output 1000.0/ms_frametime instead, which can be easier to interpret.

Also the raw results are stored in the `dt::results` object, which is a `std::vector` of this struct (for `float_type`, see below):
```c++
struct ZoneResult {
   std::string name;
   std::vector<float_type> sorted_times;
   float_type median;
   float_type mean;
   float_type worst_time;
   float_type std_dev;
};
```
The `sorted_times` holds the raw frame times in milliseconds. The other floats are derived from that and only for convenience. Technical notes: The `std_dev` is a bessel-corrected standard deviation (square root of variance).

You can check when the measurements are done and the results are ready with `bool dt::are_results_ready()` or register a callback function like so:
```c++
void result_callback(const std::vector<dt::ZoneResult>& zone_results) {
   for (const dt::ZoneResult& zone_result : zone_results) {
      // ...
   }
}

dt::set_done_callback(result_callback);
```

You can start new measurements after that. The old results will be cleared then, things will not accumulate. Optionally you can also force the removal of old results with `dt::clear_results()`, but things things will not leak if you don't.

## Fun facts
- Zones can be nested
- A zone can be used multiple times in a slice/frame. Those will then all be toggled and evaluated together as expected
- `dt.h` includes `<algorithm>`, `<cmath>`, `<string>` and `<vector>`, no external libs. By default also `<chrono>`, but see below how to prevent that
- By default `dt` uses `std::chrono::high_resolution_clock` for time measurement. Alternatively you can supply your own frame times. That is often convenient since realtime applications usually have those available anyways. Also this makes it easier to plugin any higher-performance but less portable alternatives. To do so you'll have to call `dt::slice(floating_point)` and supply it with the time since the last `dt::slice()` in milliseconds.
- You can define `DT_NO_CHRONO` if you do the above, which will prevent the `<chrono>` include und undefine the parameterless `dt::slice()` function
- By default `dt` uses doubles. If you prefer floats, just define `DT_FLOATS`. This will set the `float_type`.
- If you want to define other zones during runtime, you can call `dt::factory_reset()` to clear all zone information. That will not reset the config.

## todo
- arbitrary time units? maybe something more graceful than ms
