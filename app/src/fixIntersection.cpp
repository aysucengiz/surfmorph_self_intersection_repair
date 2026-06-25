#include "fixIntersection.h"
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <chrono> 
#include <QElapsedTimer>
#include <QProcess>   
#include <QObject>  
#include <QDebug>   
#include <QThread>                                                          
// ---------------------------------------------------------------------------
// Config loading
// ---------------------------------------------------------------------------

std::unordered_map<std::string, std::string> Repair::parseEnvFile(const std::string& path)
{
    std::unordered_map<std::string, std::string> values;
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Could not open env file: " + path);

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        auto comment = value.find(" #");
        if (comment != std::string::npos)
            value = value.substr(0, comment);

        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(value);

        if (value.size() >= 2 &&
            ((value.front() == '"'  && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
        {
            value = value.substr(1, value.size() - 2);
        }

        values[key] = value;
    }
    return values;
}

std::string Repair::require(const std::unordered_map<std::string, std::string>& map,
                             const std::string& key, const std::string& envFile)
{
    auto it = map.find(key);
    if (it == map.end() || it->second.empty())
        throw std::runtime_error("Missing required key '" + key + "' in " + envFile);
    return it->second;
}

void Repair::loadRepairConfig(const std::string& envFile)
{
    auto values  = parseEnvFile(envFile);
    m_script_loc = require(values, "REPAIR_SCRIPT_LOC", envFile);
    m_output_dir = require(values, "REPAIR_OUTPUT_DIR", envFile);
    m_host       = require(values, "REPAIR_HOST",       envFile);
    m_user       = require(values, "REPAIR_USER",       envFile);
    m_password   = require(values, "REPAIR_PASSWORD",   envFile);
    m_venv_loc   = require(values, "REMOTE_VENV",   envFile);
    m_inek_no    = "inek" + require(values, "INEK_NO",   envFile);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Repair::Repair(const std::string& envFile)
{
    loadRepairConfig(envFile);
    openConnection();
}

Repair::~Repair()
{
    closeConnection();
}

std::string Repair::quoteArg(const std::string& value)
{
    std::string quoted = "\"";
    for (char ch : value)
    {
        if (ch == '"') quoted += "\\\"";
        else           quoted += ch;
    }
    quoted += "\"";
    return quoted;
}

bool Repair::openConnection()
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start("python3", {
        "-u",
        QString::fromStdString(m_script_loc),
        "--output-dir", QString::fromStdString(m_output_dir),
        "--host",       QString::fromStdString(m_host),
        "--user",       QString::fromStdString(m_user),
        "--password",   QString::fromStdString(m_password),
        "--remote-venv",QString::fromStdString(m_venv_loc),
        "--inek-host",  QString::fromStdString(m_inek_no)
    });

    if (!m_process.waitForStarted(10000))
    {
        std::cout << "Could not start mesh repair script." << std::endl;
        return false;
    }
    std::cout << "✔ PROCESS STARTED\n";
    m_childRunning = true;

    std::string buf;

    if (!waitFor("__START__", buf, 60000))
    {
        std::cout << "Timed out waiting for repair session prompt." << std::endl;
        m_process.waitForFinished(5000);

        std::cout << "\n===== STDOUT =====\n";
        std::cout << m_process.readAllStandardOutput().toStdString();

        std::cout << "\n===== STDERR =====\n";
        std::cout << m_process.readAllStandardError().toStdString();

        std::cout << "\n===== EXIT CODE =====\n";
        std::cout << m_process.exitCode() << std::endl;
        closeConnection();
        return false;
    }
    qDebug() << "Main thread:" << QThread::currentThread();
    qDebug() << "m_process thread:" << m_process.thread();  

    std::cout << "Repair session ready." << std::endl;
    return true;
}

bool Repair::closeConnection()
{
    if (!m_childRunning) return true;

    m_process.write("q\n");
    m_process.waitForFinished(5000);
    m_process.terminate();

    if (!m_process.waitForFinished(5000))
    {
        m_process.kill();
    }

    m_childRunning = false;
    return true;
}

// ---------------------------------------------------------------------------
// Core
// ---------------------------------------------------------------------------

bool Repair::waitFor(const std::string& target,
                     std::string& accum,
                     int timeoutMs)
{
    accum.clear();

    QElapsedTimer timer;
    timer.start();

    QByteArray buffer;

    while (timer.elapsed() < timeoutMs)
    {
        if (!m_process.waitForReadyRead(100))
        {
            if (m_process.state() != QProcess::Running)
                break;
            continue;
        }

        buffer += m_process.readAllStandardOutput();

        std::string chunk = buffer.toStdString();
        std::cout << chunk << std::flush;

        accum += chunk;
        buffer.clear();

        if (accum.find(target) != std::string::npos)
            return true;
        if (m_process.state() != QProcess::Running)
        {
            std::cout << "PROCESS STOPPED\n";
            std::cout << "exit code: "
                    << m_process.exitCode()
                    << std::endl;
            break;
        }
    }

    return false;
}

std::string Repair::repairMesh(const std::string& obj_loc)
{
    std::cout
    << "repairMesh thread: "
    << QThread::currentThreadId()
    << std::endl;
    if (!m_childRunning)
    {
        std::cout << "No active repair session. Returning original mesh." << std::endl;
        return obj_loc;
    }

    m_process.write((obj_loc + "\n").c_str());

    std::string accum;
    if (!waitFor("Downloaded ", accum, 120000))
    {
        std::cout << "Timed out waiting for repair result." << std::endl;
        return obj_loc;
    }

    const std::string marker = "Downloaded ";
    size_t markerPos = accum.find(marker);
    if (markerPos == std::string::npos)
    {
        std::cout << "Mesh repair did not return a result. Returning original mesh." << std::endl;
        return obj_loc;
    }

    size_t start = markerPos + marker.size();
    size_t end   = accum.find_first_of("\r\n", start);
    return accum.substr(start, end == std::string::npos ? std::string::npos : end - start);
}