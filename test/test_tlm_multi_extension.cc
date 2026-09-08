// test_tlm_multi_extension.cc
// Phase 1d: Multi-extension API tests for tlm_generic_payload.
//
// Verifies:
//   - reset() loop-deletes ALL extensions (no leaks, slots nullified)
//   - deep_copy_from() deep-copies all extensions (per Accellera semantics)
//   - set/get/clear/release co-existence on the same payload
//   - ADR-X.2 regression: TransactionContextExt + ErrorContextExt coexist
//
// Style: 12 TEST_CASEs, tag [multi_ext] (plus [regression]/[edge] subsets).
// Catch2 v3.7.0 (precompiled catch_amalgamated.hpp).
#include <cstring>
#include <string>
#include "catch_amalgamated.hpp"
#include "core/error_category.hh"
#include "ext/error_context_ext.hh"
#include "ext/transaction_context_ext.hh"
#include "tlm/tlm_stub.hh"

using tlm::tlm_generic_payload;

namespace {
    // Test-only extension types. Distinct types → distinct T::ID → distinct slots
    // in m_extensions[]. This is what makes "multi-extension" actually multi.
    struct ExtA : public tlm::tlm_extension<ExtA> {
        int val = 0;
        explicit ExtA(int v = 0) : val(v) {
        }
        tlm_extension_base* clone() const override {
            return new ExtA(*this);
        }
        void copy_from(tlm_extension_base const& e) override {
            val = static_cast<ExtA const&>(e).val;
        }
    };
    struct ExtB : public tlm::tlm_extension<ExtB> {
        std::string name;
        explicit ExtB(std::string n = "") : name(std::move(n)) {
        }
        tlm_extension_base* clone() const override {
            return new ExtB(*this);
        }
        void copy_from(tlm_extension_base const& e) override {
            name = static_cast<ExtB const&>(e).name;
        }
    };
    struct ExtC : public tlm::tlm_extension<ExtC> {
        double pi = 3.14;
        explicit ExtC(double v = 3.14) : pi(v) {
        }
        tlm_extension_base* clone() const override {
            return new ExtC(*this);
        }
        void copy_from(tlm_extension_base const& e) override {
            pi = static_cast<ExtC const&>(e).pi;
        }
    };
} // namespace

// 1. Three distinct extensions stored on the same payload; all retrievable.
TEST_CASE("Multi-ext: set 3 different extensions, all retrievable", "[multi_ext]") {
    tlm_generic_payload p;
    p.set_extension(new ExtA{42});
    p.set_extension(new ExtB{"hello"});
    p.set_extension(new ExtC{2.71});
    REQUIRE(p.get_extension<ExtA>() != nullptr);
    REQUIRE(p.get_extension<ExtA>()->val == 42);
    REQUIRE(p.get_extension<ExtB>() != nullptr);
    REQUIRE(p.get_extension<ExtB>()->name == "hello");
    REQUIRE(p.get_extension<ExtC>() != nullptr);
    REQUIRE(p.get_extension<ExtC>()->pi == 2.71);
}

// 2. clear_extension<T> is selective: only the named slot is nulled.
TEST_CASE("Multi-ext: clear_extension<T> leaves others intact", "[multi_ext]") {
    tlm_generic_payload p;
    p.set_extension(new ExtA{1});
    p.set_extension(new ExtB{"x"});
    p.clear_extension<ExtB>();
    REQUIRE(p.get_extension<ExtA>() != nullptr);
    REQUIRE(p.get_extension<ExtA>()->val == 1);
    REQUIRE(p.get_extension<ExtB>() == nullptr);
    // ExtC was never set — should still be nullptr (no false positives).
    REQUIRE(p.get_extension<ExtC>() == nullptr);
}

// 3. set_extension returns the previous pointer (SystemC 2.0 compat).
TEST_CASE("Multi-ext: set_extension returns old value (SysC 2.0 compat)", "[multi_ext]") {
    tlm_generic_payload p;
    // First set on empty → returns nullptr.
    auto* first_set_return = p.set_extension(new ExtA{1});
    REQUIRE(first_set_return == nullptr);
    // Second set returns the first pointer; caller must delete it.
    auto* old = p.set_extension(new ExtA{2});
    REQUIRE(old != nullptr);
    REQUIRE(old->val == 1);
    REQUIRE(p.get_extension<ExtA>() != nullptr);
    REQUIRE(p.get_extension<ExtA>()->val == 2);
    delete old; // caller owns the returned old pointer
}

// 4. reset() must delete + nullify ALL extension slots (Phase 1c→1d upgrade).
TEST_CASE("Multi-ext: reset() clears ALL extensions", "[multi_ext]") {
    tlm_generic_payload p;
    p.set_extension(new ExtA{10});
    p.set_extension(new ExtB{"y"});
    p.set_extension(new ExtC{1.0});
    REQUIRE(p.get_extension<ExtA>() != nullptr);
    REQUIRE(p.get_extension<ExtB>() != nullptr);
    REQUIRE(p.get_extension<ExtC>() != nullptr);
    p.reset();
    REQUIRE(p.get_extension<ExtA>() == nullptr);
    REQUIRE(p.get_extension<ExtB>() == nullptr);
    REQUIRE(p.get_extension<ExtC>() == nullptr);
}

// 5. Static type ID is consistent (compile-time + cross-extension uniqueness).
TEST_CASE("Multi-ext: static type ID consistent (compile-time)", "[multi_ext]") {
    REQUIRE(ExtA::ID == ExtA::ID);
    REQUIRE(ExtB::ID == ExtB::ID);
    REQUIRE(ExtA::ID != ExtB::ID);
    REQUIRE(ExtB::ID != ExtC::ID);
    REQUIRE(ExtA::ID != ExtC::ID);
}

// 6. deep_copy_from duplicates every extension (Accellera tlm_gp.cpp pattern).
TEST_CASE("Multi-ext: deep_copy_from duplicates all extensions", "[multi_ext]") {
    tlm_generic_payload src;
    src.set_extension(new ExtA{99});
    src.set_extension(new ExtB{"src"});
    tlm_generic_payload dst;
    dst.deep_copy_from(src);
    REQUIRE(dst.get_extension<ExtA>() != nullptr);
    REQUIRE(dst.get_extension<ExtA>()->val == 99);
    REQUIRE(dst.get_extension<ExtA>() != src.get_extension<ExtA>()); // cloned, not aliased
    REQUIRE(dst.get_extension<ExtB>() != nullptr);
    REQUIRE(dst.get_extension<ExtB>()->name == "src");
    REQUIRE(dst.get_extension<ExtB>() != src.get_extension<ExtB>());
    // Core fields also copied.
    REQUIRE(dst.get_command() == src.get_command());
    REQUIRE(dst.get_address() == src.get_address());
}

// 7. clear_extension<T> does NOT free; caller owns the pointer.
TEST_CASE("Multi-ext: clear_extension does NOT free (caller owns)", "[multi_ext]") {
    tlm_generic_payload p;
    auto* e = new ExtA{77};
    p.set_extension(e);
    p.clear_extension<ExtA>();
    REQUIRE(p.get_extension<ExtA>() == nullptr);
    // Caller still owns `e` and must delete it (no double-free).
    delete e;
}

// 8. release_extension<T> frees the extension.
TEST_CASE("Multi-ext: release_extension frees", "[multi_ext]") {
    tlm_generic_payload p;
    p.set_extension(new ExtA{55});
    REQUIRE(p.get_extension<ExtA>() != nullptr);
    p.release_extension<ExtA>();
    REQUIRE(p.get_extension<ExtA>() == nullptr);
    // release_extension already deleted — no further action.
}

// 9. Unset extension types must return nullptr (no false positives).
TEST_CASE("Multi-ext: No extension of different type returns nullptr", "[multi_ext]") {
    tlm_generic_payload p;
    p.set_extension(new ExtA{1});
    REQUIRE(p.get_extension<ExtB>() == nullptr);
    REQUIRE(p.get_extension<ExtC>() == nullptr);
    // Sanity: ExtA itself is set.
    REQUIRE(p.get_extension<ExtA>() != nullptr);
}

// 10. Setting the same type twice: returns the first pointer, replaces slot.
TEST_CASE("Multi-ext: Two same-type set replaces, returns old", "[multi_ext]") {
    tlm_generic_payload p;
    auto* first = new ExtA{1};
    auto* second = new ExtA{2};
    p.set_extension(first);
    auto* old = p.set_extension(second);
    REQUIRE(old == first);
    delete old; // caller frees returned old
    REQUIRE(p.get_extension<ExtA>() == second);
    REQUIRE(p.get_extension<ExtA>()->val == 2);
}

// 11. ADR-X.2 regression: TransactionContextExt + ErrorContextExt on the same
//     payload. Phase 1c introduced multi-ext support; Phase 1d's deep_copy_from
//     and reset must preserve this. Both extensions co-exist independently.
TEST_CASE("Multi-ext regression: set_transaction_id + set_error_code coexist (ADR-X.2)",
          "[multi_ext][regression]") {
    tlm_generic_payload payload;
    auto* txn = new TransactionContextExt{};
    txn->transaction_id = 100;
    txn->source_module = "cpu_0";
    payload.set_extension(txn);
    auto* err = new ErrorContextExt{};
    err->error_code = ErrorCode::RESOURCE_BUFFER_FULL;
    err->error_message = "test";
    payload.set_extension(err);

    // BOTH extensions must coexist on the same payload (Phase 1c+1d enables this).
    REQUIRE(payload.get_extension<TransactionContextExt>() != nullptr);
    REQUIRE(payload.get_extension<TransactionContextExt>()->transaction_id == 100);
    REQUIRE(payload.get_extension<TransactionContextExt>()->source_module == "cpu_0");
    REQUIRE(payload.get_extension<ErrorContextExt>() != nullptr);
    REQUIRE(payload.get_extension<ErrorContextExt>()->error_code ==
            ErrorCode::RESOURCE_BUFFER_FULL);

    // reset() must clear BOTH.
    payload.reset();
    REQUIRE(payload.get_extension<TransactionContextExt>() == nullptr);
    REQUIRE(payload.get_extension<ErrorContextExt>() == nullptr);

    // deep_copy_from must replicate BOTH into a fresh payload.
    auto* txn2 = new TransactionContextExt{};
    txn2->transaction_id = 200;
    txn2->source_module = "cpu_1";
    payload.set_extension(txn2);
    auto* err2 = new ErrorContextExt{};
    err2->error_code = ErrorCode::SUCCESS;
    err2->error_message = "ok";
    payload.set_extension(err2);

    tlm_generic_payload copy;
    copy.deep_copy_from(payload);
    REQUIRE(copy.get_extension<TransactionContextExt>() != nullptr);
    REQUIRE(copy.get_extension<TransactionContextExt>()->transaction_id == 200);
    REQUIRE(copy.get_extension<TransactionContextExt>()->source_module == "cpu_1");
    REQUIRE(copy.get_extension<ErrorContextExt>() != nullptr);
    REQUIRE(copy.get_extension<ErrorContextExt>()->error_code == ErrorCode::SUCCESS);
    // The clones are distinct heap objects (not aliased to source).
    REQUIRE(copy.get_extension<TransactionContextExt>() != txn2);
    REQUIRE(copy.get_extension<ErrorContextExt>() != err2);
}

// 12. Edge: heap-allocated extensions survive reset() without UB.
//     (Documents that stack-allocated extensions would cause double-free on
//      reset() and dtor; this test only proves the heap-allocated path is safe.)
TEST_CASE("Multi-ext edge: heap-allocated extensions only (no stack UB)", "[multi_ext][edge]") {
    tlm_generic_payload p;
    auto* e = new ExtA{12345}; // heap — `this` test scope owns `e` until set
    p.set_extension(e);
    REQUIRE(p.get_extension<ExtA>() != nullptr);
    REQUIRE(p.get_extension<ExtA>()->val == 12345);
    p.reset(); // dtor-style cleanup: deletes e and nullifies the slot
    REQUIRE(p.get_extension<ExtA>() == nullptr);
    // No further access to `e` — reset() owns the deletion.
    SUCCEED("heap-allocated extension survived reset() without UB");
}
