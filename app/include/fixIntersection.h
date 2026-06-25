#pragma once
#include <string>
#include <array>
#include <iostream>
#include <unordered_map>
#include <QProcess>

struct RepairConfig
{
    std::string script_loc;
    std::string output_dir;
    std::string host;
    std::string user;
    std::string password;
};

class Repair
{
public:
    Repair(const std::string& envFile = "../.env");
    ~Repair();

    Repair(const Repair&) = delete;
    Repair& operator=(const Repair&) = delete;

    std::string repairMesh(const std::string& obj_loc);
    bool isConnected() const { return m_childRunning; }

private:
    std::string m_script_loc;
    std::string m_output_dir;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_venv_loc;
    std::string m_inek_no;

    QProcess m_process;
    bool m_childRunning = false;

    bool openConnection();
    bool closeConnection();
    bool waitFor(const std::string& target, std::string& accum, int timeoutMs = 30000);

    static std::unordered_map<std::string, std::string> parseEnvFile(const std::string& path);
    static std::string require(const std::unordered_map<std::string, std::string>& map,
                               const std::string& key, const std::string& envFile);
    void loadRepairConfig(const std::string& envFile);
    static std::string quoteArg(const std::string& value);
    std::string buildLaunchCommand() const;
};