#ifndef ANALYZER_H_
#define ANALYZER_H_

#include <iostream>
#include <fstream>
#include <unistd.h>  //usleep
#include <math.h>    //sqrt

#include "enums.h"
#include "util.h"
#include "replay.h"
#include "analysis.h"

//Size of combo buffer
// #define CB_SIZE 255

//Version number for the analyzer
const std::string ANALYZER_VERSION = "0.1.0";
//First actionable frame of the match (assuming frame 0 == internal frame -123)
const int      FIRST_FRAME   = START_FRAMES+PLAYABLE_FRAME;

const unsigned TIMER_MINS    = 8;      //Assuming a fixed 8 minute time for now (TODO: might need to change later)
const unsigned SHARK_THRES   = 15;     //Minimum frames to be out of hitstun before comboing becomes sharking
const unsigned POKE_THRES    = 30;     //Frames since either player entered hitstun to consider neutral a poke
const float    FOOTSIE_THRES = 10.0f;  //Distance cutoff between FOOTSIES and POSITIONING dynamics


namespace slip {

class Analyzer {
private:
  std::ostream* _dout; //Debug output stream

  bool get1v1Ports                (const SlippiReplay &s, Analysis *a) const;
  void analyzeInteractions        (const SlippiReplay &s, Analysis *a) const;
  void analyzeMoves               (const SlippiReplay &s, Analysis *a) const;
  void analyzePunishes            (const SlippiReplay &s, Analysis *a) const;
  void getBasicGameInfo           (const SlippiReplay &s, Analysis *a) const;
  void summarizeInteractions      (const SlippiReplay &s, Analysis *a) const;
  void computeAirtime             (const SlippiReplay &s, Analysis *a) const;
  void countLCancels              (const SlippiReplay &s, Analysis *a) const;
  void countTechs                 (const SlippiReplay &s, Analysis *a) const;
  void countLedgegrabs            (const SlippiReplay &s, Analysis *a) const;
  void countDodges                (const SlippiReplay &s, Analysis *a) const;
  void countDashdances            (const SlippiReplay &s, Analysis *a) const;
  void countAirdodgesAndWavelands (const SlippiReplay &s, Analysis *a) const;

  //Inline read-only convenience functions
  inline std::string stateName(const SlippiFrame &f) const {
    return Action::name[f.action_pre];
  }

  inline float playerDistance(const SlippiFrame &pf, const SlippiFrame &of) const {
    float xd = pf.pos_x_pre - of.pos_x_pre;
    float yd = pf.pos_y_pre - of.pos_y_pre;
    return sqrt(xd*xd+yd*yd);
  }

  inline unsigned deathDirection(const SlippiPlayer &p, const unsigned f) const {
    if (p.frame[f].action_post == Action::DeadDown)  { return Dir::DOWN; }
    if (p.frame[f].action_post == Action::DeadLeft)  { return Dir::LEFT; }
    if (p.frame[f].action_post == Action::DeadRight) { return Dir::RIGHT; }
    if (p.frame[f].action_post <  Action::Sleep)     { return Dir::UP; }
    return Dir::NEUT;
  }

  //NOTE: the next few functions do not check for valid frame indices
  //  This is technically unsafe, but boolean shortcut logic should ensure the unsafe
  //    portions never get called.
  inline bool maybeWavelanding(const SlippiPlayer &p, const unsigned f) const {
    //Code credit to Fizzi
    return p.frame[f].action_pre == Action::LandingFallSpecial && (
      p.frame[f-1].action_pre == Action::EscapeAir || (
        p.frame[f-1].action_pre >= Action::KneeBend &&
        p.frame[f-1].action_pre <= Action::FallAerialB
        )
      );
  }
  inline bool isDashdancing(const SlippiPlayer &p, const unsigned f) const {
    //Code credit to Fizzi
    //This should never thrown an exception, since we should never be in turn animation
    //  before frame 2
    return (p.frame[f].action_pre   == Action::Dash)
        && (p.frame[f-1].action_pre == Action::Turn)
        && (p.frame[f-2].action_pre == Action::Dash);
  }
  inline bool isInJumpsquat(const SlippiFrame &f) const {
    return f.action_pre == Action::KneeBend;
  }
  inline bool isSpotdodging(const SlippiFrame &f) const {
    return f.action_pre == Action::Escape;
  }
  inline bool isAirdodging(const SlippiFrame &f) const {
    return f.action_pre == Action::EscapeAir;
  }
  inline bool isDodging(const SlippiFrame &f) const {
    return (f.action_pre >= Action::EscapeF) && (f.action_pre <= Action::Escape);
  }
  inline bool inTumble(const SlippiFrame &f) const {
    return f.action_pre == Action::DamageFall;
  }
  inline bool inDamagedState(const SlippiFrame &f) const {
    return (f.action_pre >= Action::DamageHi1) && (f.action_pre <= Action::DamageFlyRoll);
  }
  inline bool inMissedTechState(const SlippiFrame &f) const {
    return (f.action_pre >= Action::DownBoundU) && (f.action_pre <= Action::DownSpotD);
  }
  //Excluding walltechs, walljumps, and ceiling techs
  inline bool inFloorTechState(const SlippiFrame &f) const {
    return (f.action_pre >= Action::DownBoundU) && (f.action_pre <= Action::PassiveStandB);
  }
  //Including walltechs, walljumps, and ceiling techs
  inline bool inTechState(const SlippiFrame &f) const {
    return (f.action_pre >= Action::DownBoundU) && (f.action_pre <= Action::PassiveCeil);
  }
  inline bool isShielding(const SlippiFrame &f) const {
    return f.flags_3 & 0x80;
  }
  inline bool isInShieldstun(const SlippiFrame &f) const {
    return f.action_pre == Action::GuardSetOff;
  }
  inline bool isGrabbed(const SlippiFrame &f) const {
    return (f.action_pre >= Action::CapturePulledHi) && (f.action_pre <= Action::CaptureFoot);
    // return f.action_pre == 0x00E3;
  }
  inline bool isThrown(const SlippiFrame &f) const {
    return (f.action_pre >= Action::ThrownF) && (f.action_pre <= Action::ThrownLwWomen);
  }
  inline bool isAirborne(const SlippiFrame &f) const {
    return f.airborne;
  }
  inline bool isInHitstun(const SlippiFrame &f) const {
    return f.flags_4 & 0x02;
  }
  inline bool isInHitlag(const SlippiFrame &f) const {
    return f.flags_2 & 0x20;
  }
  inline bool isDead(const SlippiFrame &f) const {
    return (f.flags_5 & 0x10) || f.action_pre < Action::Sleep;
  }
  inline bool isOnLedge(const SlippiFrame &f) const {
    return f.action_pre == Action::CliffWait;
  }
  inline bool isOffStage(const SlippiReplay &s, const SlippiFrame &f) const {
    return
      f.pos_x_pre >  Stage::ledge[s.stage] ||
      f.pos_x_pre < -Stage::ledge[s.stage] ||
      f.pos_y_pre <  0
      ;
  }

  //TODO: can be fairly easily optimized by changing elapsed frames at the beginning to frames left
  inline std::string frameAsTimer(unsigned fnum) const {
    int elapsed  = fnum-START_FRAMES;
    elapsed      = (elapsed < 0) ? 0 : elapsed;
    int mins     = elapsed/3600;
    int secs     = (elapsed/60)-(mins*60);
    int frames   = elapsed-(60*secs)-(3600*mins);

    //Convert from elapsed to left
    int lmins = TIMER_MINS-mins;
    if (secs > 0 || frames > 0) {
      lmins -= 1;
    }
    int lsecs = 60-secs;
    if (frames > 0) {
      lsecs -= 1;
    }
    int lframes = (frames > 0) ? 60-frames : 0;

    return "0" + std::to_string(lmins) + ":"
      + (lsecs   < 10 ? "0" : "") + std::to_string(lsecs) + ":"
      + (lframes < 6  ? "0" : "") + std::to_string(int(100*(float)lframes/60.0f));
  }
public:
  Analyzer(std::ostream* dout);
  ~Analyzer();
  Analysis* analyze(const SlippiReplay &s);
};

}

#endif /* ANALYZER_H_ */
