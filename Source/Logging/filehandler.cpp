//-----------------------------------------------------------------------------
//           Name: filehandler.cpp
//      Developer: Wolfire Games LLC
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------
#include "filehandler.h"

#include <Compat/fileio.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <vector>
#include <string>

using std::cerr;
using std::endl;
using std::flush;
using std::fstream;
using std::getline;
using std::setfill;
using std::setw;
using std::streampos;
using std::string;
using std::vector;

FileHandler::FileHandler(string path, size_t _startup_file_size, size_t _max_file_size) : m_file(),
                                                                                          startup_file_size(_startup_file_size),
                                                                                          max_file_size(_max_file_size) {
    string path_tmp = path + ".tmp";
    fstream m_temp_file;

    my_fstream_open(m_file, path.c_str(), fstream::in | fstream::binary);

    vector<streampos> line_marker_positions;
    streampos chosen_start_pos;

    if (m_file.good()) {
        streampos prevpos = m_file.tellg();
        line_marker_positions.push_back(prevpos);
        while (m_file.good()) {
            string line;
            getline(m_file, line);

            if (strcmp(line.c_str(), "START") == 0) {
                line_marker_positions.push_back(prevpos);
            }

            if (m_file.tellg() >= 0) {
                prevpos = m_file.tellg();
            }
        }

        line_marker_positions.push_back(prevpos);

        chosen_start_pos = line_marker_positions[line_marker_positions.size() - 1];
        for (int i = line_marker_positions.size() - 1; i >= 0; i--) {
            if ((prevpos - line_marker_positions[i]) < (long)startup_file_size) {
                chosen_start_pos = line_marker_positions[i];
            }
        }
        cerr << "Preserving " << prevpos - chosen_start_pos << " of data from previous log file, starting at pos " << chosen_start_pos << endl;
    }

    m_file.close();

    my_fstream_open(m_file, path.c_str(), fstream::in | fstream::binary);
    my_fstream_open(m_temp_file, path_tmp.c_str(), fstream::out | fstream::binary | fstream::trunc);

    if (m_file.good() && m_temp_file.good()) {
        m_file.seekp(chosen_start_pos);
        while (m_file.good()) {
            string line;
            getline(m_file, line);

            m_temp_file << line << endl;
        }
    }

    m_file.close();
    m_temp_file.close();

    ////////////////////////
    my_fstream_open(m_temp_file, path_tmp.c_str(), fstream::in);
    my_fstream_open(m_file, path.c_str(), fstream::out | fstream::trunc);
    if (!m_file.is_open()) {
        cerr << "FileHandler cannot open path for log output: " << path << endl;
        return;
    }

    while (m_temp_file.good()) {
        string line;
        getline(m_temp_file, line);

        if (!line.empty())
            m_file << line << endl;
    }

    m_file << "START" << endl;
    written = (size_t)std::max<std::streamoff>(0, m_file.tellp());
}

FileHandler::~FileHandler() {
    if (m_file.is_open())
        m_file.close();
}

void FileHandler::Log(LogSystem::LogType type, int row, const char* filename, const char* cat, const char* message_prefix, const char* message) {
    if (m_file.is_open()) {
        if (written > max_file_size) {
            m_file << "STOPPED WRITING BECAUSE FILE REACHED MAX LIMIT" << endl;
            m_file.close();
            cerr << "Shut down writing to log file because max file limit was reached." << endl;
        } else {
            time_t now = time(0);
            struct tm* tm = localtime(&now);
            m_file << tm->tm_year + 1900 << '/'
                   << setfill('0') << setw(2) << tm->tm_mon + 1 << '/'
                   << setfill('0') << setw(2) << tm->tm_mday
                   << ' '
                   << setfill('0') << setw(2) << tm->tm_hour
                   << ':'
                   << setfill('0') << setw(2) << tm->tm_min
                   << ':'
                   << setfill('0') << setw(2) << tm->tm_sec
                   << " ";
            m_file << message_prefix;
            m_file << message;
            written += 20 + strlen(message_prefix) + strlen(message);
            // Overgrowth Online: warnings and errors go to the OS at once, so a crash (which
            // kills the process with this buffer in it) keeps them; other lines at least
            // every quarter second (a flush for every line cost the game in fights)
            uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();
            if (type >= LogSystem::warning || now_ms - last_flush_ms >= 250) {
                m_file.flush();
                last_flush_ms = now_ms;
            }
        }
    }
}

void FileHandler::Flush() {
    if (m_file.is_open()) {
        flush(m_file);
    }
}

void FileHandler::SetMaxWriteLimit(size_t size) {
    max_file_size = size;
}
