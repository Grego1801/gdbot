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

void sendEv(int t, int c, int v) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = t; ev.code = c; ev.value = v;
    write(g_touchFd, &ev, sizeof(ev));
}

void pressTouch(int x, int y) {
    g_tid++;
    sendEv(3, 57, g_tid);
    sendEv(3, 53, x);
    sendEv(3, 54, y);
    sendEv(1, 330, 1);
    sendEv(0, 0, 0);
}

void releaseTouch() {
    sendEv(3, 57, -1);
    sendEv(1, 330, 0);
    sendEv(0, 0, 0);
}

bool loadMacro(std::string const& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    g_inputs.clear();

    std::string content(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>()
    );

    size_t pos = 0;
    while ((pos = content.find("\"frame\"", pos)) != std::string::npos) {
        MacroInput inp;
        inp.p2 = false;

        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) break;
        inp.frame = std::stoi(content.substr(colon + 1, 10));

        size_t next_frame = content.find("\"frame\"", pos + 1);

        size_t dpos = content.find("\"down\"", pos);
        if (dpos == std::string::npos || 
            (next_frame != std::string::npos && dpos > next_frame)) {
            pos++;
            continue;
        }
        dpos = content.find(':', dpos) + 1;
        while (content[dpos] == ' ') dpos++;
        inp.down = (content[dpos] == 't');

        size_t ppos = content.find("\"2p\"", pos);
        if (ppos != std::string::npos && 
            (next_frame == std::string::npos || ppos < next_frame)) {
            ppos = content.find(':', ppos) + 1;
            while (content[ppos] == ' ') ppos++;
            inp.p2 = (content[ppos] == 't');
        }

        g_inputs.push_back(inp);
        pos++;
    }

    return !g_inputs.empty();
}

class $modify(PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        g_playing = false;
        g_currentIndex = 0;

        std::string path = "/sdcard/replay/" + 
                           std::to_string(level->m_levelID.value()) + 
                           ".gdr.json";

        if (loadMacro(path)) {
            g_touchFd = open("/dev/input/event3", O_WRONLY);
            if (g_touchFd >= 0) {
                g_playing = true;
            }
        }

        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (!g_playing || g_currentIndex >= (int)g_inputs.size())
            return;

        int currentFrame = (int)(m_time * 240.0f);

        while (g_currentIndex < (int)g_inputs.size() &&
               g_inputs[g_currentIndex].frame <= currentFrame) {

            auto& inp = g_inputs[g_currentIndex];
            int x = inp.p2 ? 1440 : 720;

            if (inp.down)
                pressTouch(x, 540);
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
