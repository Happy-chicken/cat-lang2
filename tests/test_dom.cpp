#include "analysis_ctx.h"
#include "dominator.h"
#include <gtest/gtest.h>

using namespace cat;
static opt::ana::CFG build_cfg() {
  opt::ana::BlockInfo b0{0, nullptr, {}, {}, {1, 4}};
  opt::ana::BlockInfo b1{1, nullptr, {}, {}, {2, 3}};
  opt::ana::BlockInfo b2{2, nullptr, {}, {}, {5}};
  opt::ana::BlockInfo b3{3, nullptr, {}, {}, {5}};
  opt::ana::BlockInfo b4{4, nullptr, {}, {}, {6}};
  opt::ana::BlockInfo b5{5, nullptr, {}, {}, {6}};
  opt::ana::BlockInfo b6{6, nullptr, {}, {}, {7}};
  opt::ana::BlockInfo b7{7, nullptr, {}, {}, {}};
  opt::ana::CFG cfg{{b0, b1, b2, b3, b4, b5, b6, b7}, 0, 7};
  return cfg;
}


TEST(Dataflow, Dominator) {
  auto cfg = build_cfg();
  auto idoms = compute_idoms_fast(cfg);
  auto child = opt::ana::compute_dom_tree_children(idoms);

  EXPECT_EQ(child[0][0], 1);
  EXPECT_EQ(child[0][1], 4);
  EXPECT_EQ(child[0][2], 6);
  EXPECT_EQ(child[1][0], 2);
  EXPECT_EQ(child[1][1], 3);
  EXPECT_EQ(child[1][2], 5);
}
