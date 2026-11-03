// doctest for the test programs (built once, linked into each of them): the runner and a listener
// that prints the name of each test case as it starts.
#define DOCTEST_CONFIG_IMPLEMENT
#include "test.h"

namespace {

struct Names : doctest::IReporter {
  explicit Names(const doctest::ContextOptions&) {}
  void report_query(const doctest::QueryData&) override {}
  void test_run_start() override {}
  void test_run_end(const doctest::TestRunStats&) override {}
  void test_case_start(const doctest::TestCaseData& tc) override {
    printf("%s\n", tc.m_name);
    fflush(stdout);
  }
  void test_case_reenter(const doctest::TestCaseData&) override {}
  void test_case_end(const doctest::CurrentTestCaseStats&) override {}
  void test_case_exception(const doctest::TestCaseException&) override {}
  void subcase_start(const doctest::SubcaseSignature&) override {}
  void subcase_end() override {}
  void log_assert(const doctest::AssertData&) override {}
  void log_message(const doctest::MessageData&) override {}
  void test_case_skipped(const doctest::TestCaseData&) override {}
};

}  // namespace

REGISTER_LISTENER("nomi", 1, Names);

int run_tests(int argc, char** argv) {
  fflush(stdout);
  doctest::Context ctx;
  ctx.setOption("no-intro", true);
  ctx.applyCommandLine(argc, argv);
  const int res = ctx.run();
  fflush(stdout);
  return res;
}
