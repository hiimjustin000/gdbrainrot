#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/modify/LevelCell.hpp>

#include "VideoPlayer/VideoPlayer.hpp"

static bool g_hasInitPlayers = false;
static videoplayer::VideoPlayer* g_player;
static videoplayer::VideoPlayer* g_player2;

class $modify(MyMenuLayer, MenuLayer) {
	bool init() {
		if (!MenuLayer::init()) {
			return false;
		}
		if (g_hasInitPlayers) {
			return true;
		}
		g_hasInitPlayers = true;

		g_player = videoplayer::VideoPlayer::create(Mod::get()->getResourcesDir()/"video1.mpg", true);
		g_player->setPosition(Mod::get()->getSettingValue<int64_t>("player1-xpos"), Mod::get()->getSettingValue<int64_t>("player1-ypos"));
		g_player->setScale(Mod::get()->getSettingValue<double>("player1-scale"));
		g_player->setRotation(Mod::get()->getSettingValue<double>("player1-rotation"));
		g_player->setVolume(Mod::get()->getSettingValue<double>("player1-volume"));
		if (!Mod::get()->getSettingValue<bool>("player1-enabled")) {
			g_player->setVisible(false);
			g_player->pause();
		}
		g_player->setID("video-player-1");
		this->addChild(g_player);

		g_player2 = videoplayer::VideoPlayer::create(Mod::get()->getResourcesDir()/"video2.mpg", true);
		g_player2->setPosition(Mod::get()->getSettingValue<int64_t>("player2-xpos"), Mod::get()->getSettingValue<int64_t>("player2-ypos"));
		g_player2->setScale(Mod::get()->getSettingValue<double>("player2-scale"));
		g_player2->setRotation(Mod::get()->getSettingValue<double>("player2-rotation"));
		g_player2->setVolume(Mod::get()->getSettingValue<double>("player2-volume"));
		if (!Mod::get()->getSettingValue<bool>("player2-enabled")) {
			g_player2->setVisible(false);
			g_player2->pause();
		}
		g_player2->setID("video-player-2");
		this->addChild(g_player2);

		this->updateLayout();

		SceneManager::get()->keepAcrossScenes(g_player);
		SceneManager::get()->keepAcrossScenes(g_player2);

		return true;
	}
};

$execute {
    /* Video Player 1 */
    listenForSettingChanges("player1-xpos", [](int64_t value) {
        g_player->setPosition(value, Mod::get()->getSettingValue<int64_t>("player1-ypos"));
    });
    listenForSettingChanges("player1-ypos", [](int64_t value) {
        g_player->setPosition(Mod::get()->getSettingValue<int64_t>("player1-xpos"), value);
    });
    listenForSettingChanges("player1-scale", [](double value) {
        g_player->setScale(value);
    });
    listenForSettingChanges("player1-rotation", [](double value) {
        g_player->setRotation(value);
    });
    listenForSettingChanges("player1-volume", [](double value) {
        g_player->setVolume(value);
    });
    listenForSettingChanges("player1-enabled", [](bool value) {
        if (value) {
			g_player->resume();
			g_player->setVisible(true);
		} else {
			g_player->pause();
			g_player->setVisible(false);
		}
    });

    /* Video Player 2 */
    listenForSettingChanges("player2-xpos", [](int64_t value) {
        g_player2->setPosition(value, Mod::get()->getSettingValue<int64_t>("player2-ypos"));
    });
    listenForSettingChanges("player2-ypos", [](int64_t value) {
        g_player2->setPosition(Mod::get()->getSettingValue<int64_t>("player2-xpos"), value);
    });
    listenForSettingChanges("player2-scale", [](double value) {
        g_player2->setScale(value);
    });
    listenForSettingChanges("player2-rotation", [](double value) {
        g_player2->setRotation(value);
    });
    listenForSettingChanges("player2-volume", [](double value) {
        g_player2->setVolume(value);
    });
    listenForSettingChanges("player2-enabled", [](bool value) {
        if (value) {
			g_player2->resume();
			g_player2->setVisible(true);
		} else {
			g_player2->pause();
			g_player2->setVisible(false);
		}
    });
}
