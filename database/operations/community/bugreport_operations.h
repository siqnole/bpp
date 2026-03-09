#pragma once

#include "../../core/types.h"
#include "../../core/database.h"
#include <vector>
#include <cstdint>
#include <string>

namespace bronx {
namespace db {

namespace bugreport_operations {
    inline bool add_bug_report(Database* db, uint64_t user_id, const std::string& command_or_feature,
                               const std::string& reproduction_steps, const std::string& expected_behavior,
                               const std::string& actual_behavior, int64_t networth) {
        return db->add_bug_report(user_id, command_or_feature, reproduction_steps, expected_behavior, actual_behavior, networth);
    }
    inline std::vector<::bronx::db::BugReport> fetch_bug_reports(Database* db, const std::string& order_clause) {
        return db->fetch_bug_reports(order_clause);
    }
    inline bool mark_read(Database* db, uint64_t report_id) {
        return db->mark_bug_report_read(report_id);
    }
    inline bool mark_resolved(Database* db, uint64_t report_id) {
        return db->mark_bug_report_resolved(report_id);
    }
    inline bool delete_bug_report(Database* db, uint64_t report_id) {
        return db->delete_bug_report(report_id);
    }
    inline int get_count(Database* db) {
        return db->get_bug_report_count();
    }
}

} // namespace db
} // namespace bronx
