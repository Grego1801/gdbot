#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <fstream>
#include <vector>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>

using namespace geode::prelude;

struct MacroInput {
    int frame;
    bool down;
    bool p2;
};

std::vector<MacroInput> g_inputs;
int g_currentIndex = 0;
bool g_playing = false;
int g_touchFd = -1;
int g_tid = 0x5000;

void sendEvent(int t, int c, int v) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = t; ev.code = c; ev.value = v;
    write(g_touchFd, &ev, sizeof(ev));
}

void pressTouch(int x, int y) {
    g_tid++;
    sendEvent(3, 57, g_tid);  // TRACKING_ID
    sendEvent(3, 53, x);      // X
    sendEvent(3, 54, y);      // Y
    sendEvent(1, 330, 1);     // BTN_TOUCH down
    sendEvent(0, 0, 0);       // SYN
}

void releaseTouch() {
    sendEvent(3, 57, -1);     // TRACKING_ID release
    sendEvent(1, 330, 0);     // BTN_TOUCH up
    sendEvent(0, 0, 0);       // SYN
}

bool loadMacro(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    
    g_inputs.clear();
    
    // Parse JSON manually (simple)
    std::string line;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    
    size_t pos = 0;
    while ((pos = content.find("\"frame\"", pos)) != std::string::npos) {
        MacroInput inp;
        
        // frame
        pos = content.find(':', pos) + 1;
        inp.frame = std::stoi(content.substr(pos, 10));
        
        // down
        auto dpos = content.find("\"down\"", pos);
        auto fpos = content.find("\"frame\"", pos + 1);
        if (dpos == std::string::npos || (fpos != std::string::npos && dpos > fpos))
            break;
        dpos = content.find(':', dpos) + 1;
        while (content[dpos] == ' ') dpos++;
        inp.down = content[dpos] == 't';
        
        // 2p
        auto ppos = content.find("\"2p\"", pos);
        if (ppos != std::string::npos && (fpos == std::string::npos || ppos < fpos)) {
            ppos = content.find(':', ppos) + 1;
            while (content[ppos] == ' ') ppos++;
            inp.p2 = content[ppos] == 't';
        } else {
            inp.p2 = false;
        }
        
        g_inputs.push_back(inp);
        pos++;
    }
    
    log::info("Loaded {} inputs from {}", g_inputs.size(), path);
    return true;
}

class $modify(PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;
        
        // Load macro
        std::string macroPath = "/sdcard/replay/" + 
                                std::to_string(level->m_levelID) + ".gdr.json";
        
        if (loadMacro(macroPath)) {
            g_currentIndex = 0;
            g_playing = true;
            
            // Open touch device
            g_touchFd = open("/dev/input/event3", O_WRONLY);
            if (g_touchFd < 0) {
                log::error("Cannot open touch device");
                g_playing = false;
            } else {
                log::info("MacroBot: ready with {} inputs", g_inputs.size());
            }
        }
        
        return true;
    }
    
    void update(float dt) {
        PlayLayer::update(dt);
        
        if (!g_playing || g_currentIndex >= (int)g_inputs.size())
            return;
        
        // Get current frame from GD's internal timer
        int currentFrame = (int)(m_time * 240.0f);
        
        // Execute all inputs up to current frame
        while (g_currentIndex < (int)g_inputs.size() &&
               g_inputs[g_currentIndex].frame <= currentFrame) {
            
            auto& inp = g_inputs[g_currentIndex];
            
            // P1 = left side, P2 = right side (horizontal 4:3)
            int x = inp.p2 ? 1440 : 720;
            int y = 540;
            
            if (inp.down)
                pressTouch(x, y);
            else
                releaseTouch();
            
            g_currentIndex++;
        }
    }
    
    void onQuit() {
        g_playing = false;
        if (g_touchFd >= 0) {
            close(g_touchFd);
            g_touchFd = -1;
        }
        PlayLayer::onQuit();
    }
};
