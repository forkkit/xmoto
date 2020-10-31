/*=============================================================================
XMOTO

This file is part of XMOTO.

XMOTO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

XMOTO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XMOTO; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

/*
 *  Input handler
 */
#include "Input.h"
#include "GameText.h"
#include "common/VFileIO.h"
#include "db/xmDatabase.h"
#include "helpers/Log.h"
#include "helpers/Text.h"
#include "helpers/VExcept.h"
#include <sstream>
#include <utility>

InputHandler::InputHandler() {
  reset();
}

void InputHandler::reset() {
  resetScriptKeyHooks();
}

bool InputHandler::areJoysticksEnabled() const {
  return SDL_JoystickEventState(SDL_QUERY) == SDL_ENABLE;
}

void InputHandler::enableJoysticks(bool i_value) {
  SDL_JoystickEventState(i_value ? SDL_ENABLE : SDL_IGNORE);
}

/*===========================================================================
Init/uninit
===========================================================================*/
void InputHandler::init(UserConfig *pConfig,
                        xmDatabase *pDb,
                        const std::string &i_id_profile,
                        bool i_enableJoysticks) {
  /* Initialize joysticks (if any) */
  SDL_InitSubSystem(SDL_INIT_JOYSTICK);

  enableJoysticks(i_enableJoysticks);

  /* Open all joysticks */
  recheckJoysticks();
  loadConfig(pConfig, pDb, i_id_profile);
}

void InputHandler::uninit(void) {
  /* Close all joysticks */
  for (unsigned int i = 0; i < m_Joysticks.size(); i++) {
    SDL_JoystickClose(m_Joysticks[i]);
  }

  /* No more joysticking */
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

/**
 * converts a raw joystick axis value to a float, according to specified minimum
 * and maximum values, as well as the deadzone.
 *
 *                 (+)      ____
 *           result |      /|
 *                  |     / |
 *                  |    /  |
 *  (-)________ ____|___/___|____(+)
 *             /|   |   |   |    input
 *            / |   |   |   |
 *           /  |   |   |   |
 *     _____/   |   |   |   |
 *          |   |  (-)  |   |
 *         neg  dead-zone  pos
 *
 */
float InputHandler::joyRawToFloat(float raw,
                                  float neg,
                                  float deadzone_neg,
                                  float deadzone_pos,
                                  float pos) {
  if (neg > pos) {
    std::swap(neg, pos);
    std::swap(deadzone_neg, deadzone_pos);
  }

  if (raw > pos)
    return +1.0f;
  if (raw > deadzone_pos)
    return +((raw - deadzone_pos) / (pos - deadzone_pos));
  if (raw < neg)
    return -1.0f;
  if (raw < deadzone_neg)
    return -((raw - deadzone_neg) / (neg - deadzone_neg));

  return 0.0f;
}

/*===========================================================================
Read configuration
===========================================================================*/
void InputHandler::loadConfig(UserConfig *pConfig,
                              xmDatabase *pDb,
                              const std::string &i_id_profile) {
  std::string v_key;

  /* Set defaults */
  setDefaultConfig();

  /* To preserve backward compatibility with SDL1.2,
   * we copy the keys and prefix them with "_" */
  const std::string prefix = sdl12CompatIsUpgraded(pDb, i_id_profile) ? "_" : "";

  /* Get settings for mode */
  for (unsigned int i = 0; i < INPUT_NB_PLAYERS; i++) {
    std::ostringstream v_n;
    v_n << (i + 1);

    for (unsigned int j = 0; j < INPUT_NB_PLAYERKEYS; j++) {
      try {
        m_playerKeys[i][j].key =
          XMKey(pDb->config_getString(i_id_profile,
                                      prefix + m_playerKeys[i][j].name + v_n.str(),
                                      m_playerKeys[i][j].key.toString()));
      } catch (InvalidSystemKeyException &e) {
        /* keep default key */
      } catch (Exception &e) {
        // load default key (undefined) to not override the key while
        // undefined keys are not saved to avoid config brake in case
        // you forgot to plug your joystick
        m_playerKeys[i][j].key = XMKey();
      }
    }

    // script keys
    for (unsigned int k = 0; k < MAX_SCRIPT_KEY_HOOKS; k++) {
      std::ostringstream v_k;
      v_k << (k);

      v_key = pDb->config_getString(
        i_id_profile, prefix + "KeyActionScript" + v_n.str() + "_" + v_k.str(), "");
      if (v_key != "") { // don't override the default key if there is nothing
        // in the config
        try {
          m_nScriptActionKeys[i][k] = XMKey(v_key);
        } catch (InvalidSystemKeyException &e) {
          /* keep default key */
        } catch (Exception &e) {
          m_nScriptActionKeys[i][k] = XMKey();
        }
      }
    }
  }

  // global keys
  for (unsigned int i = 0; i < INPUT_NB_GLOBALKEYS; i++) {
    try {
      m_globalKeys[i].key = XMKey(pDb->config_getString(
        i_id_profile, prefix + m_globalKeys[i].name, m_globalKeys[i].key.toString()));
    } catch (InvalidSystemKeyException &e) {
      /* keep default key */
    } catch (Exception &e) {
      m_globalKeys[i].key = XMKey();
    }
  }
}

/*===========================================================================
Add script key hook
===========================================================================*/
void InputHandler::addScriptKeyHook(Scene *pGame,
                                    const std::string &keyName,
                                    const std::string &FuncName) {
  if (m_nNumScriptKeyHooks < MAX_SCRIPT_KEY_HOOKS) {
    m_ScriptKeyHooks[m_nNumScriptKeyHooks].FuncName = FuncName;

    if (keyName.size() == 1) { /* old basic mode */
      m_ScriptKeyHooks[m_nNumScriptKeyHooks].nKey = XMKey(keyName, true);
    } else {
      m_ScriptKeyHooks[m_nNumScriptKeyHooks].nKey = XMKey(keyName);
    }
    m_ScriptKeyHooks[m_nNumScriptKeyHooks].pGame = pGame;
    m_nNumScriptKeyHooks++;
  }
}

int InputHandler::getNumScriptKeyHooks() const {
  return m_nNumScriptKeyHooks;
}

InputScriptKeyHook InputHandler::getScriptKeyHooks(int i) const {
  return m_ScriptKeyHooks[i];
}

XMKey InputHandler::getScriptActionKeys(int i_player,
                                        int i_actionScript) const {
  return m_nScriptActionKeys[i_player][i_actionScript];
}

std::string *InputHandler::getJoyId(Uint8 i_joynum) {
  return &(m_JoysticksIds[i_joynum]);
}

Uint8 InputHandler::getJoyNum(const std::string &i_name) {
  for (unsigned int i = 0; i < m_JoysticksIds.size(); i++) {
    if (m_JoysticksIds[i] == i_name) {
      return i;
    }
  }
  throw Exception("Invalid joystick name");
}

std::string *InputHandler::getJoyIdByStrId(const std::string &i_name) {
  for (unsigned int i = 0; i < m_JoysticksIds.size(); i++) {
    if (m_JoysticksIds[i] == i_name) {
      return &(m_JoysticksIds[i]);
    }
  }
  throw Exception("Invalid joystick name");
}

SDL_Joystick *InputHandler::getJoyById(std::string *i_id) {
  for (unsigned int i = 0; i < m_JoysticksIds.size(); i++) {
    if (&(m_JoysticksIds[i]) == i_id) {
      return m_Joysticks[i];
    }
  }
  throw Exception("Invalid joystick id");
}

InputEventType InputHandler::joystickAxisSens(Sint16 m_joyAxisValue) {
  return abs(m_joyAxisValue) < INPUT_JOYSTICK_MINIMUM_DETECTION ? INPUT_UP
                                                                : INPUT_DOWN;
}

IFullKey InputHandler::getDefaultPlayerKey(int player, int key) {
  switch (player) {
    case 0: {
      switch (key) {
        case INPUT_DRIVE:     return IFullKey("KeyDrive",     XMKey(SDLK_UP,    KMOD_NONE), GAMETEXT_DRIVE);
        case INPUT_BRAKE:     return IFullKey("KeyBrake",     XMKey(SDLK_DOWN,  KMOD_NONE), GAMETEXT_BRAKE);
        case INPUT_FLIPLEFT:  return IFullKey("KeyFlipLeft",  XMKey(SDLK_LEFT,  KMOD_NONE), GAMETEXT_FLIPLEFT);
        case INPUT_FLIPRIGHT: return IFullKey("KeyFlipRight", XMKey(SDLK_RIGHT, KMOD_NONE), GAMETEXT_FLIPRIGHT);
        case INPUT_CHANGEDIR: return IFullKey("KeyChangeDir", XMKey(SDLK_SPACE, KMOD_NONE), GAMETEXT_CHANGEDIR);
      }
      break;
    }
    case 1: {
      switch (key) {
        case INPUT_DRIVE:     return IFullKey("KeyDrive",     XMKey(SDLK_a,     KMOD_NONE), GAMETEXT_DRIVE);
        case INPUT_BRAKE:     return IFullKey("KeyBrake",     XMKey(SDLK_q,     KMOD_NONE), GAMETEXT_BRAKE);
        case INPUT_FLIPLEFT:  return IFullKey("KeyFlipLeft",  XMKey(SDLK_z,     KMOD_NONE), GAMETEXT_FLIPLEFT);
        case INPUT_FLIPRIGHT: return IFullKey("KeyFlipRight", XMKey(SDLK_e,     KMOD_NONE), GAMETEXT_FLIPRIGHT);
        case INPUT_CHANGEDIR: return IFullKey("KeyChangeDir", XMKey(SDLK_w,     KMOD_NONE), GAMETEXT_CHANGEDIR);
      }
      break;
    }
    case 2: {
      switch (key) {
        case INPUT_DRIVE:     return IFullKey("KeyDrive",     XMKey(SDLK_r,     KMOD_NONE), GAMETEXT_DRIVE);
        case INPUT_BRAKE:     return IFullKey("KeyBrake",     XMKey(SDLK_f,     KMOD_NONE), GAMETEXT_BRAKE);
        case INPUT_FLIPLEFT:  return IFullKey("KeyFlipLeft",  XMKey(SDLK_t,     KMOD_NONE), GAMETEXT_FLIPLEFT);
        case INPUT_FLIPRIGHT: return IFullKey("KeyFlipRight", XMKey(SDLK_y,     KMOD_NONE), GAMETEXT_FLIPRIGHT);
        case INPUT_CHANGEDIR: return IFullKey("KeyChangeDir", XMKey(SDLK_v,     KMOD_NONE), GAMETEXT_CHANGEDIR);
      }
      break;
    }
    case 3: {
      switch (key) {
        case INPUT_DRIVE:     return IFullKey("KeyDrive",     XMKey(SDLK_u,     KMOD_NONE), GAMETEXT_DRIVE);
        case INPUT_BRAKE:     return IFullKey("KeyBrake",     XMKey(SDLK_j,     KMOD_NONE), GAMETEXT_BRAKE);
        case INPUT_FLIPLEFT:  return IFullKey("KeyFlipLeft",  XMKey(SDLK_i,     KMOD_NONE), GAMETEXT_FLIPLEFT);
        case INPUT_FLIPRIGHT: return IFullKey("KeyFlipRight", XMKey(SDLK_o,     KMOD_NONE), GAMETEXT_FLIPRIGHT);
        case INPUT_CHANGEDIR: return IFullKey("KeyChangeDir", XMKey(SDLK_k,     KMOD_NONE), GAMETEXT_CHANGEDIR);
      }
      break;
    }
  }

  return IFullKey();
}

IFullKey InputHandler::getDefaultGlobalKey(int key) {
  switch (key) {
    case INPUT_SWITCHUGLYMODE:             return IFullKey("KeySwitchUglyMode",             XMKey(SDLK_F9,             KMOD_NONE),             GAMETEXT_SWITCHUGLYMODE);
    case INPUT_SWITCHBLACKLIST:            return IFullKey("KeySwitchBlacklist",            XMKey(SDLK_b,              KMOD_LCTRL),            GAMETEXT_SWITCHBLACKLIST);
    case INPUT_SWITCHFAVORITE:             return IFullKey("KeySwitchFavorite",             XMKey(SDLK_F3,             KMOD_NONE),             GAMETEXT_SWITCHFAVORITE);
    case INPUT_RESTARTLEVEL:               return IFullKey("KeyRestartLevel",               XMKey(SDLK_RETURN,         KMOD_NONE),             GAMETEXT_RESTARTLEVEL);
    case INPUT_SHOWCONSOLE:                return IFullKey("KeyShowConsole",                XMKey(SDL_SCANCODE_GRAVE,  KMOD_NONE),             GAMETEXT_SHOWCONSOLE);
    case INPUT_CONSOLEHISTORYPLUS:         return IFullKey("KeyConsoleHistoryPlus",         XMKey(SDLK_PLUS,           KMOD_LCTRL),            GAMETEXT_CONSOLEHISTORYPLUS);
    case INPUT_CONSOLEHISTORYMINUS:        return IFullKey("KeyConsoleHistoryMinus",        XMKey(SDLK_MINUS,          KMOD_LCTRL),            GAMETEXT_CONSOLEHISTORYMINUS);
    case INPUT_RESTARTCHECKPOINT:          return IFullKey("KeyRestartCheckpoint",          XMKey(SDLK_BACKSPACE,      KMOD_NONE),             GAMETEXT_RESTARTCHECKPOINT);
    case INPUT_CHAT:                       return IFullKey("KeyChat",                       XMKey(SDLK_c,              KMOD_LCTRL),            GAMETEXT_CHATDIALOG);
    case INPUT_CHATPRIVATE:                return IFullKey("KeyChatPrivate",                XMKey(SDLK_p,              KMOD_LCTRL),            GAMETEXT_CHATPRIVATEDIALOG);
    case INPUT_LEVELWATCHING:              return IFullKey("KeyLevelWatching",              XMKey(SDLK_TAB,            KMOD_NONE),             GAMETEXT_LEVELWATCHING);
    case INPUT_SWITCHPLAYER:               return IFullKey("KeySwitchPlayer",               XMKey(SDLK_F2,             KMOD_NONE),             GAMETEXT_SWITCHPLAYER);
    case INPUT_SWITCHTRACKINGSHOTMODE:     return IFullKey("KeySwitchTrackingshotMode",     XMKey(SDLK_F4,             KMOD_NONE),             GAMETEXT_SWITCHTRACKINGSHOTMODE);
    case INPUT_NEXTLEVEL:                  return IFullKey("KeyNextLevel",                  XMKey(SDLK_PAGEUP,         KMOD_NONE),             GAMETEXT_NEXTLEVEL);
    case INPUT_PREVIOUSLEVEL:              return IFullKey("KeyPreviousLevel",              XMKey(SDLK_PAGEDOWN,       KMOD_NONE),             GAMETEXT_PREVIOUSLEVEL);
    case INPUT_SWITCHRENDERGHOSTTRAIL:     return IFullKey("KeySwitchRenderGhosttrail",     XMKey(SDLK_g,              KMOD_LCTRL),            GAMETEXT_SWITCHREDERGHOSTTRAIL);
    case INPUT_SCREENSHOT:                 return IFullKey("KeyScreenshot",                 XMKey(SDLK_F12,            KMOD_NONE),             GAMETEXT_SCREENSHOT);
    case INPUT_SWITCHWWWACCESS:            return IFullKey("KeySwitchWWWAccess",            XMKey(SDLK_F8,             KMOD_NONE),             GAMETEXT_SWITCHWWWACCESS);
    case INPUT_SWITCHFPS:                  return IFullKey("KeySwitchFPS",                  XMKey(SDLK_F7,             KMOD_NONE),             GAMETEXT_SWITCHFPS);
    case INPUT_SWITCHGFXQUALITYMODE:       return IFullKey("KeySwitchGFXQualityMode",       XMKey(SDLK_F10,            KMOD_NONE),             GAMETEXT_SWITCHGFXQUALITYMODE);
    case INPUT_SWITCHGFXMODE:              return IFullKey("KeySwitchGFXMode",              XMKey(SDLK_F11,            KMOD_NONE),             GAMETEXT_SWITCHGFXMODE);
    case INPUT_SWITCHNETMODE:              return IFullKey("KeySwitchNetMode",              XMKey(SDLK_n,              KMOD_LCTRL),            GAMETEXT_SWITCHNETMODE);
    case INPUT_SWITCHHIGHSCOREINFORMATION: return IFullKey("KeySwitchHighscoreInformation", XMKey(SDLK_w,              KMOD_LCTRL),            GAMETEXT_SWITCHHIGHSCOREINFORMATION);
    case INPUT_NETWORKADMINCONSOLE:        return IFullKey("KeyNetworkAdminConsole",        XMKey(SDLK_s, (SDL_Keymod)(KMOD_LCTRL|KMOD_LALT)), GAMETEXT_NETWORKADMINCONSOLE);
    case INPUT_SWITCHSAFEMODE:             return IFullKey("KeySafeMode",                   XMKey(SDLK_F6,             KMOD_NONE),             GAMETEXT_SWITCHSAFEMODE);

    /* uncustomizable keys */
    case INPUT_HELP:                       return IFullKey("KeyHelp",                       XMKey(SDLK_F1,             KMOD_NONE),             GAMETEXT_HELP,                false);
    case INPUT_RELOADFILESTODB:            return IFullKey("KeyReloadFilesToDb",            XMKey(SDLK_F5,             KMOD_NONE),             GAMETEXT_RELOADFILESTODB,     false);
    // don't set it to true while ESCAPE is not setable via the option as a key
    case INPUT_PLAYINGPAUSE:               return IFullKey("KeyPlayingPause",               XMKey(SDLK_ESCAPE,         KMOD_NONE),             GAMETEXT_PLAYINGPAUSE,        false);
    case INPUT_KILLPROCESS:                return IFullKey("KeyKillProcess",                XMKey(SDLK_k,              KMOD_LCTRL),            GAMETEXT_KILLPROCESS,         false);
    case INPUT_REPLAYINGREWIND:            return IFullKey("KeyReplayingRewind",            XMKey(SDLK_LEFT,           KMOD_NONE),             GAMETEXT_REPLAYINGREWIND,     false);
    case INPUT_REPLAYINGFORWARD:           return IFullKey("KeyReplayingForward",           XMKey(SDLK_RIGHT,          KMOD_NONE),             GAMETEXT_REPLAYINGFORWARD,    false);
    case INPUT_REPLAYINGPAUSE:             return IFullKey("KeyReplayingPause",             XMKey(SDLK_SPACE,          KMOD_NONE),             GAMETEXT_REPLAYINGPAUSE,      false);
    case INPUT_REPLAYINGSTOP:              return IFullKey("KeyReplayingStop",              XMKey(SDLK_ESCAPE,         KMOD_NONE),             GAMETEXT_REPLAYINGSTOP,       false);
    case INPUT_REPLAYINGFASTER:            return IFullKey("KeyReplayingFaster",            XMKey(SDLK_UP,             KMOD_NONE),             GAMETEXT_REPLAYINGFASTER,     false);
    case INPUT_REPLAYINGABITFASTER:        return IFullKey("KeyReplayingABitFaster",        XMKey(SDLK_UP,             KMOD_LCTRL),            GAMETEXT_REPLAYINGABITFASTER, false);
    case INPUT_REPLAYINGSLOWER:            return IFullKey("KeyReplayingSlower",            XMKey(SDLK_DOWN,           KMOD_NONE),             GAMETEXT_REPLAYINGSLOWER,     false);
    case INPUT_REPLAYINGABITSLOWER:        return IFullKey("KeyReplayingABitSlower",        XMKey(SDLK_DOWN,           KMOD_LCTRL),            GAMETEXT_REPLAYINGABITSLOWER, false);
  }

  return IFullKey();
}


/*===========================================================================
Set totally default configuration - useful for when something goes wrong
===========================================================================*/
void InputHandler::setDefaultConfig() {
  for (int p = 0; p < INPUT_NB_PLAYERS; ++p) {
    for (int key = 0; key < INPUT_NB_PLAYERKEYS; ++key) {
      m_playerKeys[p][key] = getDefaultPlayerKey(p, key);
    }
  }

  for (int key = 0; key < INPUT_NB_GLOBALKEYS; ++key) {
    m_globalKeys[key] = getDefaultGlobalKey(key);
  }
}

void InputHandler::sdl12CompatMap(IFullKey &fkey, IFullKey defaultKey,
    const std::unordered_map<int32_t, int32_t> &map) {

  SDL_Keycode keycode = fkey.key.getKeyboardSym();
  /* handle SDL1.2 "world keys" (SDLK_WORLD_0..SDLK_WORLD_95) */
  if (keycode >= 160 && keycode <= 255) {
    fkey = defaultKey;
    return;
  }

  auto it = map.find(keycode);

  if (it != map.end())
    // key modifiers are the same between SDL 1.2 and 2.0,
    // no need to do anything about them
    fkey.key = XMKey(it->second, fkey.key.getKeyboardMod());
}

void InputHandler::sdl12CompatUpgrade() {
  std::string file = "compat/sdl12-keytable.txt";
  FileHandle *pfh = XMFS::openIFile(FDT_DATA, file);
  if (!pfh) {
    std::string err = "Failed to read " + file;
    LogError(err.c_str());
    throw new Exception(err);
  }
  std::unordered_map<int32_t, int32_t> map;

  std::string line;
  while (XMFS::readNextLine(pfh, line)) {
    if (line.empty() || line.rfind("#", 0) == 0)
      continue;

    std::vector<std::string> tokens = splitStr(line, ' ');
    if (tokens.size() > 1) {
      int32_t a = atol(tokens[0].c_str());
      int32_t b = atol(tokens[1].c_str());

      if (a != SDLK_UNKNOWN && b != SDLK_UNKNOWN)
        map.insert(std::pair<int32_t, int32_t>(a, b));
    }
  }

  XMFS::closeFile(pfh);

  for (int p = 0; p < INPUT_NB_PLAYERS; ++p) {
    for (int key = 0; key < INPUT_NB_PLAYERKEYS; ++key) {
      auto &fkey = m_playerKeys[p][key];
      sdl12CompatMap(fkey, getDefaultPlayerKey(p, key), map);
    }
  }

  for (int key = 0; key < INPUT_NB_GLOBALKEYS; ++key) {
    auto &fkey = m_globalKeys[key];
    sdl12CompatMap(fkey, getDefaultGlobalKey(key), map);
  }

  for (int p = 0; p < INPUT_NB_PLAYERS; ++p) {
    for (int key = 0; key < MAX_SCRIPT_KEY_HOOKS; ++key) {
      IFullKey fkey;
      sdl12CompatMap(fkey, fkey, map);
      m_nScriptActionKeys[p][key] = fkey.key;
    }
  }
}

bool InputHandler::sdl12CompatIsUpgraded(xmDatabase *pDb,
                                         const std::string &i_id_profile) const {
  return !pDb->config_getBool(i_id_profile, "NotifyKeyCompatUpgrade", true);
}


/*===========================================================================
Get key by action...
===========================================================================*/

std::string InputHandler::getKeyByAction(const std::string &Action,
                                         bool i_tech) {
  for (unsigned int i = 0; i < INPUT_NB_PLAYERS; i++) {
    std::ostringstream v_n;

    if (i != 0) { // nothing for player 0
      v_n << " " << (i + 1);
    }

    if (Action == "Drive" + v_n.str())
      return i_tech ? m_playerKeys[i][INPUT_DRIVE].key.toString()
                    : m_playerKeys[i][INPUT_DRIVE].key.toFancyString();
    if (Action == "Brake" + v_n.str())
      return i_tech ? m_playerKeys[i][INPUT_BRAKE].key.toString()
                    : m_playerKeys[i][INPUT_BRAKE].key.toFancyString();
    if (Action == "PullBack" + v_n.str())
      return i_tech ? m_playerKeys[i][INPUT_FLIPLEFT].key.toString()
                    : m_playerKeys[i][INPUT_FLIPLEFT].key.toFancyString();
    if (Action == "PushForward" + v_n.str())
      return i_tech ? m_playerKeys[i][INPUT_FLIPRIGHT].key.toString()
                    : m_playerKeys[i][INPUT_FLIPRIGHT].key.toFancyString();
    if (Action == "ChangeDir" + v_n.str())
      return i_tech ? m_playerKeys[i][INPUT_CHANGEDIR].key.toString()
                    : m_playerKeys[i][INPUT_CHANGEDIR].key.toFancyString();
  }
  return "?";
}

void InputHandler::saveConfig(UserConfig *pConfig,
                              xmDatabase *pDb,
                              const std::string &i_id_profile) {
  pDb->config_setValue_begin();

  const std::string prefix = "_";

  for (unsigned int i = 0; i < INPUT_NB_PLAYERS; i++) {
    std::ostringstream v_n;
    v_n << (i + 1);

    // player keys
    for (unsigned int j = 0; j < INPUT_NB_PLAYERKEYS; j++) {
      if (m_playerKeys[i][j].key.isDefined()) {
        pDb->config_setString(i_id_profile,
                              prefix + m_playerKeys[i][j].name + v_n.str(),
                              m_playerKeys[i][j].key.toString());
      }
    }

    // script keys
    for (unsigned int k = 0; k < MAX_SCRIPT_KEY_HOOKS; k++) {
      if (m_nScriptActionKeys[i][k].isDefined()) {
        std::ostringstream v_k;
        v_k << (k);

        pDb->config_setString(i_id_profile,
                              prefix + "KeyActionScript" + v_n.str() + "_" + v_k.str(),
                              m_nScriptActionKeys[i][k].toString());
      }
    }
  }

  for (unsigned int i = 0; i < INPUT_NB_GLOBALKEYS; i++) {
    pDb->config_setString(
      i_id_profile, prefix + m_globalKeys[i].name, m_globalKeys[i].key.toString());
  }

  pDb->config_setValue_end();
}

void InputHandler::setSCRIPTACTION(int i_player, int i_action, XMKey i_value) {
  m_nScriptActionKeys[i_player][i_action] = i_value;
}

XMKey InputHandler::getSCRIPTACTION(int i_player, int i_action) const {
  return m_nScriptActionKeys[i_player][i_action];
}

void InputHandler::setGlobalKey(unsigned int INPUT_key, XMKey i_value) {
  m_globalKeys[INPUT_key].key = i_value;
}

const XMKey *InputHandler::getGlobalKey(unsigned int INPUT_key) const {
  return &(m_globalKeys[INPUT_key].key);
}

std::string InputHandler::getGlobalKeyHelp(unsigned int INPUT_key) const {
  return m_globalKeys[INPUT_key].help;
}

bool InputHandler::getGlobalKeyCustomizable(unsigned int INPUT_key) const {
  return m_globalKeys[INPUT_key].customizable;
}

void InputHandler::setPlayerKey(unsigned int INPUT_key,
                                int i_player,
                                XMKey i_value) {
  m_playerKeys[i_player][INPUT_key].key = i_value;
}

const XMKey *InputHandler::getPlayerKey(unsigned int INPUT_key,
                                        int i_player) const {
  return &(m_playerKeys[i_player][INPUT_key].key);
}

std::string InputHandler::getPlayerKeyHelp(unsigned int INPUT_key,
                                           int i_player) const {
  return m_playerKeys[i_player][INPUT_key].help;
}

bool InputHandler::isANotGameSetKey(XMKey *i_xmkey) const {
  for (unsigned int i = 0; i < INPUT_NB_PLAYERS; i++) {
    for (unsigned int j = 0; j < INPUT_NB_PLAYERKEYS; j++) {
      if ((*getPlayerKey(j, i)) == *i_xmkey) {
        return false;
      }
    }

    for (unsigned int k = 0; k < MAX_SCRIPT_KEY_HOOKS; k++) {
      if (m_nScriptActionKeys[i][k] == *i_xmkey)
        return false;
    }
  }
  return true;
}

void InputHandler::recheckJoysticks() {
  std::string v_joyName, v_joyId;
  int n;
  bool v_continueToOpen = true;
  SDL_Joystick *v_joystick;

  m_Joysticks.clear();
  m_JoysticksNames.clear();
  m_JoysticksIds.clear();

  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (v_continueToOpen) {
      if ((v_joystick = SDL_JoystickOpen(i)) != NULL) {
        std::ostringstream v_id;
        n = 0;
        v_joyName = SDL_JoystickName(v_joystick);

        // check if there is an other joystick with the same name
        for (unsigned int j = 0; j < m_Joysticks.size(); j++) {
          if (m_JoysticksNames[j] == v_joyName) {
            n++;
          }
        }

        if (n > 0) {
          v_id << " " << (n + 1); // +1 to get an id name starting at 1
        }
        v_joyId = v_joyName + v_id.str();
        m_Joysticks.push_back(v_joystick);
        m_JoysticksNames.push_back(v_joyName);
        m_JoysticksIds.push_back(v_joyId);

        LogInfo("Joystick found [%s], id is [%s]",
                v_joyName.c_str(),
                v_joyId.c_str());
      } else {
        v_continueToOpen = false; // don't continue to open joystick to keep
        // m_joysticks[joystick.num] working
        LogWarning("fail to open joystick [%s], abort to open other joysticks",
                   v_joyName.c_str());
      }
    }
  }
}

std::vector<std::string> &InputHandler::getJoysticksNames() {
  return m_JoysticksNames;
}

IFullKey::IFullKey(const std::string &i_name,
                   const XMKey &i_key,
                   const std::string i_help,
                   bool i_customisable) {
  name = i_name;
  key = i_key;
  help = i_help;
  customizable = i_customisable;
}

IFullKey::IFullKey() {}
