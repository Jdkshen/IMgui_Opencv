#include "SpcDatabase.h"

#include <windows.h>

#include <algorithm>
#include <mutex>

namespace
{
struct sqlite3;
struct sqlite3_stmt;

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;
constexpr int SQLITE_OPEN_READWRITE = 0x00000002;
constexpr int SQLITE_OPEN_CREATE = 0x00000004;
constexpr int SQLITE_OPEN_FULLMUTEX = 0x00010000;
constexpr int SQLITE_TRANSIENT_VALUE = -1;

using SqliteDestructor = void(__cdecl*)(void*);
using OpenV2Fn = int(__cdecl*)(const char*, sqlite3**, int, const char*);
using CloseFn = int(__cdecl*)(sqlite3*);
using ExecFn = int(__cdecl*)(sqlite3*, const char*, int(__cdecl*)(void*, int, char**, char**), void*, char**);
using FreeFn = void(__cdecl*)(void*);
using PrepareFn = int(__cdecl*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
using FinalizeFn = int(__cdecl*)(sqlite3_stmt*);
using StepFn = int(__cdecl*)(sqlite3_stmt*);
using BindInt64Fn = int(__cdecl*)(sqlite3_stmt*, int, long long);
using BindDoubleFn = int(__cdecl*)(sqlite3_stmt*, int, double);
using BindTextFn = int(__cdecl*)(sqlite3_stmt*, int, const char*, int, SqliteDestructor);
using ColumnInt64Fn = long long(__cdecl*)(sqlite3_stmt*, int);
using ColumnDoubleFn = double(__cdecl*)(sqlite3_stmt*, int);
using ColumnTextFn = const unsigned char*(__cdecl*)(sqlite3_stmt*, int);
using ErrorMessageFn = const char*(__cdecl*)(sqlite3*);

struct SqliteApi
{
    HMODULE module = nullptr;
    OpenV2Fn openV2 = nullptr;
    CloseFn close = nullptr;
    ExecFn exec = nullptr;
    FreeFn freeMemory = nullptr;
    PrepareFn prepare = nullptr;
    FinalizeFn finalize = nullptr;
    StepFn step = nullptr;
    BindInt64Fn bindInt64 = nullptr;
    BindDoubleFn bindDouble = nullptr;
    BindTextFn bindText = nullptr;
    ColumnInt64Fn columnInt64 = nullptr;
    ColumnDoubleFn columnDouble = nullptr;
    ColumnTextFn columnText = nullptr;
    ErrorMessageFn errorMessage = nullptr;
};

std::mutex s_mutex;
SqliteApi s_api;
sqlite3* s_database = nullptr;
SpcDatabaseSnapshot s_snapshot;

template <typename T>
bool LoadProc(T& target, const char* name)
{
    target = reinterpret_cast<T>(GetProcAddress(s_api.module, name));
    return target != nullptr;
}

bool EnsureApi()
{
    if (s_api.module)
        return true;
    s_api.module = LoadLibraryW(L"winsqlite3.dll");
    if (!s_api.module)
    {
        s_snapshot.lastError = "Windows SQLite 运行库不可用";
        return false;
    }
    const bool loaded =
        LoadProc(s_api.openV2, "sqlite3_open_v2") &&
        LoadProc(s_api.close, "sqlite3_close") &&
        LoadProc(s_api.exec, "sqlite3_exec") &&
        LoadProc(s_api.freeMemory, "sqlite3_free") &&
        LoadProc(s_api.prepare, "sqlite3_prepare_v2") &&
        LoadProc(s_api.finalize, "sqlite3_finalize") &&
        LoadProc(s_api.step, "sqlite3_step") &&
        LoadProc(s_api.bindInt64, "sqlite3_bind_int64") &&
        LoadProc(s_api.bindDouble, "sqlite3_bind_double") &&
        LoadProc(s_api.bindText, "sqlite3_bind_text") &&
        LoadProc(s_api.columnInt64, "sqlite3_column_int64") &&
        LoadProc(s_api.columnDouble, "sqlite3_column_double") &&
        LoadProc(s_api.columnText, "sqlite3_column_text") &&
        LoadProc(s_api.errorMessage, "sqlite3_errmsg");
    if (!loaded)
    {
        s_snapshot.lastError = "Windows SQLite API 不完整";
        FreeLibrary(s_api.module);
        s_api = {};
        return false;
    }
    s_snapshot.available = true;
    return true;
}

std::string DatabaseError()
{
    return s_database && s_api.errorMessage
        ? std::string(s_api.errorMessage(s_database))
        : std::string("SQLite 操作失败");
}

bool ExecuteSql(const char* sql)
{
    char* error = nullptr;
    const int result = s_api.exec(s_database, sql, nullptr, nullptr, &error);
    if (result == SQLITE_OK)
        return true;
    s_snapshot.lastError = error ? error : DatabaseError();
    if (error)
        s_api.freeMemory(error);
    return false;
}

std::string ColumnText(sqlite3_stmt* statement, int column)
{
    const unsigned char* value = s_api.columnText(statement, column);
    return value ? reinterpret_cast<const char*>(value) : std::string();
}
}

namespace SpcDatabase
{
bool Open(const std::string& path)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_database && s_snapshot.path == path)
        return true;
    if (!EnsureApi())
        return false;
    if (s_database)
    {
        s_api.close(s_database);
        s_database = nullptr;
    }
    const int result = s_api.openV2(path.c_str(), &s_database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK)
    {
        s_snapshot.lastError = DatabaseError();
        if (s_database)
            s_api.close(s_database);
        s_database = nullptr;
        s_snapshot.open = false;
        return false;
    }
    s_snapshot.path = path;
    s_snapshot.open = true;
    s_snapshot.lastError.clear();
    return ExecuteSql("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; "
        "CREATE TABLE IF NOT EXISTS spc_samples ("
        "sequence INTEGER PRIMARY KEY, timestamp TEXT NOT NULL, tool_id INTEGER NOT NULL, "
        "tool_name TEXT NOT NULL, measurement_name TEXT NOT NULL, value REAL NOT NULL, "
        "unit TEXT NOT NULL, status INTEGER NOT NULL); "
        "CREATE INDEX IF NOT EXISTS idx_spc_measurement_sequence "
        "ON spc_samples(measurement_name, sequence);");
}

void Close()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_database)
        s_api.close(s_database);
    s_database = nullptr;
    s_snapshot.open = false;
}

bool Append(const SpcDatabaseRecord& record)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_database)
        return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT OR REPLACE INTO spc_samples "
        "(sequence,timestamp,tool_id,tool_name,measurement_name,value,unit,status) "
        "VALUES (?,?,?,?,?,?,?,?);";
    if (s_api.prepare(s_database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        s_snapshot.lastError = DatabaseError();
        return false;
    }
    const auto transient = reinterpret_cast<SqliteDestructor>(
        static_cast<std::intptr_t>(SQLITE_TRANSIENT_VALUE));
    s_api.bindInt64(statement, 1, static_cast<long long>(record.sequence));
    s_api.bindText(statement, 2, record.timestamp.c_str(), -1, transient);
    s_api.bindInt64(statement, 3, static_cast<long long>(record.toolId));
    s_api.bindText(statement, 4, record.toolName.c_str(), -1, transient);
    s_api.bindText(statement, 5, record.measurementName.c_str(), -1, transient);
    s_api.bindDouble(statement, 6, record.value);
    s_api.bindText(statement, 7, record.unit.c_str(), -1, transient);
    s_api.bindInt64(statement, 8, static_cast<long long>(record.status));
    const int result = s_api.step(statement);
    s_api.finalize(statement);
    if (result != SQLITE_DONE)
    {
        s_snapshot.lastError = DatabaseError();
        return false;
    }
    ++s_snapshot.insertedRecords;
    return true;
}

std::vector<SpcDatabaseRecord> LoadRecent(std::size_t maximumRecords)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    std::vector<SpcDatabaseRecord> records;
    if (!s_database || maximumRecords == 0)
        return records;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT sequence,timestamp,tool_id,tool_name,measurement_name,value,unit,status "
        "FROM spc_samples ORDER BY sequence DESC LIMIT ?;";
    if (s_api.prepare(s_database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        s_snapshot.lastError = DatabaseError();
        return records;
    }
    s_api.bindInt64(statement, 1, static_cast<long long>(maximumRecords));
    while (s_api.step(statement) == SQLITE_ROW)
    {
        SpcDatabaseRecord record;
        record.sequence = static_cast<std::uint64_t>(s_api.columnInt64(statement, 0));
        record.timestamp = ColumnText(statement, 1);
        record.toolId = static_cast<std::uint64_t>(s_api.columnInt64(statement, 2));
        record.toolName = ColumnText(statement, 3);
        record.measurementName = ColumnText(statement, 4);
        record.value = s_api.columnDouble(statement, 5);
        record.unit = ColumnText(statement, 6);
        record.status = static_cast<ToolResultStatus>(s_api.columnInt64(statement, 7));
        records.push_back(std::move(record));
    }
    s_api.finalize(statement);
    std::reverse(records.begin(), records.end());
    return records;
}

bool Clear()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_database && ExecuteSql("DELETE FROM spc_samples;");
}

SpcDatabaseSnapshot Snapshot()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_snapshot;
}
}
