// test/test_coherence_domain.cc
// Phase 4.2: CoherenceDomain TDD Unit Tests
// TDD Red Phase: Tests that should fail until implementation is complete

#include <string>
#include <vector>
#include "core/coherence_domain.hh"
#include "core/sim_object.hh"
#include <catch2/catch_all.hpp>

// Test helper to create a mock event queue for SimObject
class MockEventQueue : public EventQueue {
public:
    MockEventQueue() = default;
};

TEST_CASE("CoherenceDomain can be instantiated", "[coherence][phase4]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    REQUIRE(domain != nullptr);
    REQUIRE(domain->getName() == "test_domain");
}

TEST_CASE("CoherenceDomain set_protocol MESI", "[coherence][protocol]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    bool result = domain->set_protocol(Protocol::MESI);
    REQUIRE(result == true);
}

TEST_CASE("CoherenceDomain set_protocol MOESI", "[coherence][protocol]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    bool result = domain->set_protocol(Protocol::MOESI);
    REQUIRE(result == true);
}

TEST_CASE("CoherenceDomain set_members", "[coherence][members]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    std::vector<std::string> members = {"cpu0", "cpu1", "l2_cache"};
    bool result = domain->set_members(members);

    REQUIRE(result == true);
    REQUIRE(domain->is_member("cpu0") == true);
    REQUIRE(domain->is_member("cpu1") == true);
    REQUIRE(domain->is_member("l2_cache") == true);
}

TEST_CASE("CoherenceDomain is_member returns false for non-member", "[coherence][members]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    std::vector<std::string> members = {"cpu0", "cpu1"};
    domain->set_members(members);

    REQUIRE(domain->is_member("cpu0") == true);
    REQUIRE(domain->is_member("cpu2") == false);
    REQUIRE(domain->is_member("invalid") == false);
}

TEST_CASE("CoherenceDomain set_snoop_fanout", "[coherence][snoop]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    bool result = domain->set_snoop_fanout(4);
    REQUIRE(result == true);
}

TEST_CASE("CoherenceDomain get_snoop_targets returns all members", "[coherence][snoop]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    std::vector<std::string> members = {"cpu0", "cpu1", "cpu2"};
    domain->set_members(members);

    auto targets = domain->get_snoop_targets();

    REQUIRE(targets.size() == 3);
    REQUIRE(std::find(targets.begin(), targets.end(), "cpu0") != targets.end());
    REQUIRE(std::find(targets.begin(), targets.end(), "cpu1") != targets.end());
    REQUIRE(std::find(targets.begin(), targets.end(), "cpu2") != targets.end());
}

TEST_CASE("CoherenceDomain lookup_home_node returns valid node", "[coherence][directory]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    domain->set_protocol(Protocol::MESI);
    domain->set_members({"cpu0", "cpu1", "l2_cache"});

    std::string home_node = domain->lookup_home_node(0x1000);
    REQUIRE(!home_node.empty());
}

TEST_CASE("CoherenceDomain inherits from SimObject", "[coherence][inheritance]") {
    MockEventQueue eq;
    auto domain = std::make_unique<CoherenceDomain>("test_domain", &eq);

    // Verify it's a SimObject
    SimObject* sim = dynamic_cast<SimObject*>(domain.get());
    REQUIRE(sim != nullptr);
}