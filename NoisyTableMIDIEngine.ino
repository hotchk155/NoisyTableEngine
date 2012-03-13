////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////
//// NOISY TABLE MIDI ENGINE
////
//// J.Hotchkiss 2012
////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// MIDI channels (zero indexed in the protocol: i.e. MIDI CHAN 1 is actually 0)
enum {
    CHAN_1 = 0,
    CHAN_2,
    CHAN_3,
    CHAN_4,
    CHAN_5,
    CHAN_6,
    CHAN_7,
    CHAN_8,
    CHAN_9,
    CHAN_10,
    CHAN_11,
    CHAN_12,
    CHAN_13,
    CHAN_14,
    CHAN_15,
    CHAN_16
};

// Assign MIDI channels to roles 
#define CHAN_MONO1       CHAN_1
#define CHAN_POLY1       CHAN_2
#define CHAN_ARP1        CHAN_3
#define CHAN_EVT1        CHAN_4
#define CHAN_MONO2       CHAN_5
#define CHAN_POLY2       CHAN_6
#define CHAN_ARP2        CHAN_7
#define CHAN_EVT2        CHAN_8
#define CHAN_GLOBAL      CHAN_9

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////
////
////
////                       M I D I   I / O
////
////
////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// state variables
byte midiInRunningStatus;
byte midiOutRunningStatus;
byte midiNumParams;
byte midiParams[2];
byte midiSendChannel;

// macros
#define MIDI_IS_NOTE_ON(msg) ((msg & 0xf0) == 0x90)  
#define MIDI_NOTE_ON 0x90
#define MIDI_PARAM_TIMEOUT  50  // max ms we will wait for a mandatory midi parameter to arrive

// realtime synch messages
#define MIDI_SYNCH_TICK     0xf8
#define MIDI_SYNCH_START    0xfa
#define MIDI_SYNCH_CONTINUE 0xfb
#define MIDI_SYNCH_STOP     0xfc
void midiInit()
{
  // init the serial port
  Serial.begin(31250);
  Serial.flush();

  midiInRunningStatus = 0;
  midiOutRunningStatus = 0;
  midiNumParams = 0;
  midiSendChannel = 0;
}

////////////////////////////////////////////////////////////////////////////////
// MIDI WRITE
void midiWrite(byte statusByte, byte param1, byte param2, byte numParams)
{
  Serial.write(statusByte);
  if(numParams > 0)
    Serial.write(param1);
   if(numParams > 1)
     Serial.write(param2);    
}

////////////////////////////////////////////////////////////////////////////////
// MIDI READ / THRU
byte midiRead()
{
  // is anything available?
  if(Serial.available())
  {
    // read next character
    byte ch = Serial.read();

    // Is it a status byte
    if((ch & 0x80)>0)
    {
      // Interpret the status byte
      switch(ch & 0xf0)
      {
      case 0x80: //  Note-off  2  key  velocity  
      case 0x90: //  Note-on  2  key  veolcity  
      case 0xA0: //  Aftertouch  2  key  touch  
        midiInRunningStatus = ch;
        midiNumParams = 2;
        break;

      case 0xB0: //  Continuous controller  2  controller #  controller value  
      case 0xC0: //  Patch change  2  instrument #   
      case 0xE0: //  Pitch bend  2  lsb (7 bits)  msb (7 bits)  
        midiInRunningStatus = ch;
        midiNumParams = 2;
        break;

      case 0xD0: //  Channel Pressure  1  pressure  
        midiInRunningStatus = ch;
        midiNumParams = 1;
        break;

      case 0xF0: //  Realtime etc, no params
        return ch; 
      }
    }

    // do we have an active message
    if(midiInRunningStatus)
    {
      // read params for the message
      for(int thisParam = 0; thisParam < midiNumParams; ++thisParam)
      {
        // they might not have arrived yet!
        if(!Serial.available())
        {
          // if the next param is not ready then we need to wait
          // for it... but not forever (we don't want to hang)
          unsigned long midiTimeout = millis() + MIDI_PARAM_TIMEOUT;
          while(!Serial.available())
          {
            if(millis() > midiTimeout)
              return 0;
          }
        }
        midiParams[thisParam] = Serial.read();
      }

      // return the status byte (caller will read params from global variables)
      return midiInRunningStatus;
    }
  }

  // nothing pending
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// MIDI SEND REALTIME
void midiSendRealTime(byte msg)
{
  Serial.write(msg);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////
////
////
////              M I D I   C O N T R O L L E R S
////
////
////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Flags for controller events
#define O_NOINIT   0x01    // the CC is not reset 
#define O_INIT64   0x02    // CC is reset to value 64 (halfway pos)
#define O_INIT127  0x04    // CC is reset to value 127 (full scale)
#define O_SHUFFLE  0x08    // CC is reset to a random value
#define O_SWEEP    0x10    // CC is "swept" between values
#define O_SWITCH   0x20    // CC is an ON/OFF switch with values of 0 and 127 only
#define O_RUN      0x40    // internally used to indicate sweep in progress

// This class defines MIDI continuous controller 
class CController {
  byte m_msg;
  byte m_cc;
  byte m_val;
  byte m_target;
public:  
  byte m_opts;
  byte m_id;
  
  //
  // constructor
  //
  CController(byte chan, byte cc, byte id, byte opts)
  {
    opts &= ~O_RUN;
    if(!!(opts & O_SWITCH))
      opts &= ~O_SWEEP;
   
    m_msg = 0xb0 | chan;
    m_cc = cc;
    m_id = id;
    m_opts = opts;    
    m_target = 255;
    m_val = 0;
  }
  
  //
  // reinitialise at start of rally
  //
  void init()
  {
    byte val = 0;
    m_target = 255;
    if(!!(m_opts & O_NOINIT))
      return;      
    if(!!(m_opts & O_INIT64))
      val = 64;
    if(!!(m_opts & O_INIT127))
      val = 127;
    m_val = 255;
    set(val);
  }
  
  //
  // set to a new value
  // 
  void set(int val)
  {
    if(!!(m_opts & O_SHUFFLE))
    {
      if(!!(m_opts & O_SWITCH))
        val = random(2);        
      else
        val = random(128);        
    }
    else
    {
      val = constrain(val,0,127);
    }
    if(val != m_val)
    {
      if(!!(m_opts & O_SWITCH))
        val = val? 127:0;
      if(!!(m_opts & O_SWEEP))
      {
        m_target = val;
        m_opts |= O_RUN;
      }
      else
      {
        m_val = val;
        midiWrite(m_msg, m_cc, m_val, 2);
      }
    }
  }
  
  //
  // run the controller (used to implement
  // controllers that glide between values)
  //
  void run()
  {

    if(!!(m_opts & O_RUN))
    {
      if(m_val != m_target)
      {
        float a = (m_target - m_val)/4.0;
        if(fabs(a) < 1)      
          a = (a<0) ? -1:1;
        m_val = constrain(m_val + a, 0, 127);
        midiWrite(m_msg, m_cc, m_val, 2);
      }
      else
      {
        m_opts &= ~O_RUN;
      }
    }
  }
};

// Declare the array of controllers
extern CController *Controllers[];

///////////////////////////////////////////////////////////////////
// Init controllers
void initControllers()
{
  CController **p = Controllers;
  while(*p)
  {
    (*p)->init();
    p++;
  }
}

///////////////////////////////////////////////////////////////////
// Assign controller... implement glide option
void setController(byte id, int value)
{
  CController **p = Controllers;
  while(*p)
  {
    if((*p)->m_id == id)
      (*p)->set(value);    
    p++;
  }
}

///////////////////////////////////////////////////////////////////
// Run controllers... implement glide option
void runControllers()
{
  CController **p = Controllers;
  while(*p)
  {
    if(((*p)->m_opts & O_RUN))
      (*p)->run();    
    p++;
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////
////
////
////                        E V E N T   C L A S S
////
////
////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define EVENTS_POLL_DELAY 10  // how many ms to wait between event counter decrements
#define EVENT_ON_COUNT 10  // number of event polls that note event is "on"

// Flags for note events
#define EO_P1      0x01    // event goes to player 1 event channel
#define EO_P2      0x02    // event goes to player 2 event channel
#define EO_RUN     0x04    // event "on" flag - used internally

// Class to define a "note event". This is a MIDI note which is used by the MIDI
// engine to define some special meaning like "end of the rally"
class CNoteEvent
{
  byte m_count;
public:  
  byte m_note;
  byte m_mask;
public:
  CNoteEvent(byte note)
  {
    m_note = note;
    m_count = 0;
    m_mask = 0;
  }
  void init()
  {
    unfire();
  }
  void fire(byte mask)
  {
    // sent to global chan
    midiWrite(MIDI_NOTE_ON|CHAN_GLOBAL, m_note, 127, 2);        
    if(!!(mask & EO_P1))
      midiWrite(MIDI_NOTE_ON|CHAN_EVT1, m_note, 127, 2);    
    if(!!(mask & EO_P2))
      midiWrite(MIDI_NOTE_ON|CHAN_EVT2, m_note, 127, 2);    
    m_count = EVENT_ON_COUNT;
    m_mask = mask|EO_RUN;
  }
  void unfire()
  {
    midiWrite(MIDI_NOTE_ON|CHAN_GLOBAL, m_note, 0, 2);        
    if(!!(m_mask & EO_P1))
      midiWrite(MIDI_NOTE_ON|CHAN_EVT1, m_note, 0, 2);    
    if(!!(m_mask & EO_P2))
      midiWrite(MIDI_NOTE_ON|CHAN_EVT2, m_note, 0, 2);    
    m_count = 0;
    m_mask = 0;
  }
  void run()
  {
    if(m_count)
      --m_count;
    if(!m_count)
      unfire();
  }
};

// declare array of events
extern CNoteEvent *Events[];

///////////////////////////////////////////////////////////////////
// Init events
void initEvents()
{
  CNoteEvent **p = Events;
  while(*p)
  {
    (*p)->init();
    p++;
  }
}

///////////////////////////////////////////////////////////////////
// fire an event
void fireEvent(byte note, byte mask)
{
  CNoteEvent **p = Events;
  while(*p)
  {
    if((*p)->m_note == note)
    {
      (*p)->fire(mask);    
      break;
    }
    p++;
  }
}

///////////////////////////////////////////////////////////////////
// Run events
void runEvents()
{
  CNoteEvent **p = Events;
  while(*p)
  {
    if(((*p)->m_mask & EO_RUN))
      (*p)->run();    
    p++;
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//
//
//     M I D I   N O T E   A N D   C O N T R O L L E R   M A P P I N G
// 
//                         D E F I N I T I O N S
//
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// The controller events we support
enum {  
  // continuous
  C_INIT, // place holder for random values assigned on new rally
  C_BOUNCE,
  C_X1,
  C_X2,
  C_DX1,
  C_DX2,
  C_Y1,
  C_Y2,
  C_DY2,
  C_TEMPO,
  C_RALLYLEN,  
  C_RALLYLENMOD3,  
  C_RALLYLENMOD5,  
  C_RALLYLENMOD7,  
  C_RALLYLENMOD9,  
  C_ENDGAME,

  // switches  
  C_TIMEOUT,
  C_MISREAD,
  C_DROPPED,
  C_SLOWERPACE,
  C_SAMEPACE,
  C_FASTERPACE,
  C_DODGY
};  

// CC mappings for reason combinator
enum {
  ROTARY_1 = 71,
  ROTARY_2 = 72,
  ROTARY_3 = 73,
  ROTARY_4 = 74,
  SWITCH_1 = 75,
  SWITCH_2 = 76,
  SWITCH_3 = 77,
  SWITCH_4 = 78
};

// map controller events to specific controllers
#define MAP_CC(chan, cc, id, opts)  new CController(chan, cc, id, opts)
CController *Controllers[] = {
  MAP_CC(  CHAN_MONO1,   ROTARY_1,   C_X1, O_SWEEP),
  MAP_CC(  CHAN_MONO1,   ROTARY_2,   C_DX1, O_SWEEP),
  MAP_CC(  CHAN_MONO1,   ROTARY_3,   C_Y1, O_SWEEP),
  MAP_CC(  CHAN_MONO1,   ROTARY_4,   C_INIT, O_SHUFFLE),
  MAP_CC(  CHAN_MONO1,   SWITCH_1,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_MONO1,   SWITCH_2,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_MONO1,   SWITCH_3,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_MONO1,   SWITCH_4,   C_INIT, O_SHUFFLE|O_SWITCH),

  MAP_CC(  CHAN_MONO2,   ROTARY_1,   C_X2, O_SWEEP),
  MAP_CC(  CHAN_MONO2,   ROTARY_2,   C_DX2, O_SWEEP),
  MAP_CC(  CHAN_MONO2,   ROTARY_3,   C_Y2, O_SWEEP),
  MAP_CC(  CHAN_MONO2,   ROTARY_4,   C_INIT, O_SHUFFLE),
  MAP_CC(  CHAN_MONO2,   SWITCH_1,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_MONO2,   SWITCH_2,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_MONO2,   SWITCH_3,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_MONO2,   SWITCH_4,   C_INIT, O_SHUFFLE|O_SWITCH),
  
  MAP_CC(  CHAN_POLY1,   ROTARY_1,   C_TEMPO, 0),
  MAP_CC(  CHAN_POLY1,   ROTARY_2,   C_RALLYLEN, 0),
  MAP_CC(  CHAN_POLY1,   ROTARY_3,   C_INIT, O_SHUFFLE),
  MAP_CC(  CHAN_POLY1,   ROTARY_4,   C_INIT, O_SHUFFLE),
  MAP_CC(  CHAN_POLY1,   SWITCH_1,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_POLY1,   SWITCH_2,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_POLY1,   SWITCH_3,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_POLY1,   SWITCH_4,   C_INIT, O_SHUFFLE|O_SWITCH),

  MAP_CC(  CHAN_POLY2,   ROTARY_1,   C_TEMPO, 0),
  MAP_CC(  CHAN_POLY2,   ROTARY_2,   C_RALLYLEN, 0),
  MAP_CC(  CHAN_POLY2,   ROTARY_3,   C_INIT, O_SHUFFLE),
  MAP_CC(  CHAN_POLY2,   ROTARY_4,   C_INIT, O_SHUFFLE),
  MAP_CC(  CHAN_POLY2,   SWITCH_1,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_POLY2,   SWITCH_2,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_POLY2,   SWITCH_3,   C_INIT, O_SHUFFLE|O_SWITCH),
  MAP_CC(  CHAN_POLY2,   SWITCH_4,   C_INIT, O_SHUFFLE|O_SWITCH),
  
  MAP_CC(  CHAN_GLOBAL,   ROTARY_1,   C_TEMPO, 0),
  MAP_CC(  CHAN_GLOBAL,   ROTARY_2,   C_RALLYLEN, 0),
  MAP_CC(  CHAN_GLOBAL,   ROTARY_3,   C_ENDGAME, 0),
  MAP_CC(  CHAN_GLOBAL,   ROTARY_4,   C_INIT, O_SHUFFLE),
  MAP_CC(  CHAN_GLOBAL,   SWITCH_1,   C_TIMEOUT, O_SWITCH),
  MAP_CC(  CHAN_GLOBAL,   SWITCH_2,   C_DODGY, O_SWITCH),
  MAP_CC(  CHAN_GLOBAL,   SWITCH_3,   C_DROPPED, O_SWITCH),
  MAP_CC(  CHAN_GLOBAL,   SWITCH_4,   C_INIT, O_SWITCH),
  NULL
};  


// The note events we support
enum {
  E_READY = 31,
  E_FIRSTBOUNCE = 32,
  E_BOUNCE = 33,
  E_DROPPED = 34,
  E_TIMEOUT = 35,
  E_MISREAD = 36
};

// set up table of note events
#define MAP_EV(note)  new CNoteEvent(note)
CNoteEvent *Events[] = {
  MAP_EV(E_READY),
  MAP_EV(E_FIRSTBOUNCE),
  MAP_EV(E_DROPPED),
  MAP_EV(E_BOUNCE),
  MAP_EV(E_TIMEOUT),
  MAP_EV(E_MISREAD),
  NULL
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//
//
//                   A R P E G G I A T O R   C L A S S 
//
//
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#define ARP_SEQ_LEN 32 // length of note sequence
class CArpeggiator
{
  byte arpSeq[ARP_SEQ_LEN];
  byte m_events;
  byte stepLen;
  byte stepCount;
  byte index;
  byte m_lastNote;
  byte m_chan;
  byte m_ticks;
  unsigned long m_nextNoteTime;
public:  
  CArpeggiator(byte chan)
  {
    m_chan = chan;
    init();
  }

  void init()
  {
    for(int i=0;i<ARP_SEQ_LEN;++i)
      arpSeq[i] = 0;
    m_events = 0;
    m_nextNoteTime = 0;
    index = 0;
    m_lastNote = 0;
    m_ticks = 0;
  }

  void stopLastNote()
  {
    if(m_lastNote)
    {
      midiWrite(MIDI_NOTE_ON|m_chan, m_lastNote, 0, 2);
      m_lastNote = 0;
    }
  }

  void addNote(byte note)
  {
    byte inserts = 1;
    switch(m_events++)
    {
    case 0:
      inserts = 4;
      break;
    case 1:
    case 2:
      inserts = 2;
      break;
    }
    for(int i=0;i<inserts;++i)
      arpSeq[random(ARP_SEQ_LEN)] = note;
  }

  void run()
  {
      if(!m_ticks)
      {
        if(m_lastNote)
          midiWrite(MIDI_NOTE_ON|m_chan, m_lastNote, 0, 2);
        m_lastNote = arpSeq[index];
        index = (index + 1) % ARP_SEQ_LEN;
        if(m_lastNote)
          midiWrite(MIDI_NOTE_ON|m_chan, m_lastNote, 127, 2);
      }
      m_ticks = (m_ticks+1)%24;
  } 
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//
//
//                  M E T R O N O M E   C L A S S
//
//
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#define TEMPO_DROP_DELAY 500      // how long after last bounce that tempo starts to drop
#define TEMPO_DROP_RATE 100       // how many ms between new tempo drop values
#define METRONOME_TEMPOCC_MAX 220 // how many bpm map to the highest CC value for game pace
class CMetronome {

  unsigned long lastEvent;
  unsigned long nextBeatClock;
  unsigned long beatInterval;
  unsigned long tempoDropInterval;
  unsigned long nextTempoDrop;
  float runningBPM;

public:

  CMetronome() 
  {
    init();
  }

  void init()
  {
    lastEvent = 0;
    nextBeatClock = 0;
    beatInterval = 0;
    runningBPM = 0;
    nextTempoDrop = 0;
    tempoDropInterval = 0;
    midiSendRealTime(MIDI_SYNCH_STOP);
  }

  // call when BPM changes
  void updateBPM()
  {
    beatInterval = (60000.0 / 24.0) / runningBPM;
    setController(C_TEMPO, (127 * runningBPM) / METRONOME_TEMPOCC_MAX);
  }

  /////////////////////////////////////////////////////////////////////////
  // called when the metronome is being started
  void start(unsigned long m)
  {
    // send the MIDI start
    lastEvent = 0;
    midiSendRealTime(MIDI_SYNCH_START);
    runningBPM = 60;
    updateBPM();
    event(m,0);
  }
  
  /////////////////////////////////////////////////////////////////////////
  // when an event occurs to push the metronome
  // assume at two beat intervals
  void event(unsigned long m, byte whichPlayer)
  {    
    // do we have a previous event to check?
    if(lastEvent)
    {
      unsigned long deltaTime = m - lastEvent; // ms since last event
      if(deltaTime > 0)
      {
        // change the running BPM
        float calcBPM = (float)240000.0 / deltaTime;
        if(runningBPM > 1)
        {
          float diff = calcBPM/runningBPM;          
          if(diff < 1.0)
          {
            setController(C_SLOWERPACE, 0);
            setController(C_SAMEPACE, 0);
            setController(C_FASTERPACE, 0);
            setController(C_DODGY, 1);
          }
          else if(diff < 1.4)
          {
            setController(C_SLOWERPACE, 1);
            setController(C_SAMEPACE, 0);
            setController(C_FASTERPACE, 0);
            setController(C_DODGY, 0);
          }
          else if(diff > 1.8)
          {
            setController(C_SLOWERPACE, 0);
            setController(C_SAMEPACE, 0);
            setController(C_FASTERPACE, 1);
            setController(C_DODGY, 0);
          }
          else
          {
            setController(C_SLOWERPACE, 0);
            setController(C_SAMEPACE, 1);
            setController(C_FASTERPACE, 0);
            setController(C_DODGY, 0);
          }          
        }
        runningBPM = 0.9 * runningBPM + 0.1 * calcBPM;
        updateBPM();
      }
    }
      
    // store last event time
    lastEvent = m;
    nextTempoDrop = m + TEMPO_DROP_DELAY;
    nextBeatClock = m + beatInterval;
  }

  /////////////////////////////////////////////////////////////////////////
  // RUN METRONOME
  byte run(unsigned long m, byte holdTempo)
  {
    // time to slacken the tempo?
    if(nextTempoDrop && !holdTempo && m > nextTempoDrop)
    {
      // tempo reduce 
      runningBPM = 0.95 * runningBPM;
      updateBPM();
      if(runningBPM > 30)
      {
        // if greater than min BPM we keep ticking
        nextTempoDrop = m + TEMPO_DROP_RATE;        
      }
      else      
      {
        // stop the clock
        midiSendRealTime(MIDI_SYNCH_STOP);
        nextBeatClock = 0;
        nextTempoDrop =  0;
      }
    }
    
    // time for next MIDI beat clock?
    if(nextBeatClock && m > nextBeatClock)
    {
      midiSendRealTime(MIDI_SYNCH_TICK);
      nextBeatClock = m + beatInterval;
      return 1;
    }
    else
    {
      return 0;
    }
  }
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//
// 
//       P O L Y P H O N I C   C H A N N E L   C H O R D   H O L D E R 
//
//
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#define CHORD_LEN 4 // number of notes to hold
class CChordHolder
{
  byte m_chan;
  byte chord[CHORD_LEN];
public:  
  CChordHolder(byte chan)
  {
    m_chan = chan;
    memset(chord, 255, sizeof(chord));
  }
  void init()
  {
    for(int i=0; i<CHORD_LEN; ++i)
    {  
      if(chord[i] != 255)
        midiWrite(MIDI_NOTE_ON|m_chan, chord[i], 0, 2);     
    }    
  }
  void addNote(byte note)
  {
    int i;
    note = constrain(note,0,127);
    midiWrite(MIDI_NOTE_ON|m_chan, note, 127, 2);     
    for(i=0; i<CHORD_LEN; ++i)
    {  
      if(chord[i] == note)
        return;
    }
    
    if(chord[CHORD_LEN-1] != 255)
    {
      midiWrite(MIDI_NOTE_ON|m_chan, chord[CHORD_LEN-1], 0, 2);     
    }
    for(i=CHORD_LEN-1; i>0; --i)
      chord[i] = chord[i-1];
    chord[0] = note;
  }
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//
//
//                        N O T E   M A P P I N G
//
//
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
class CNoteMap
{
public:
  byte note[64];
  byte m_lastNote;
  byte m_chan;
  void randomize(byte root, byte *scale, byte scaleLen)
  {
    for(int i=0; i<32; ++i)
    {
      int thisNote = root + scale[random(scaleLen)];
      if(thisNote > 127) 
        thisNote = 127;        
      note[8 * (i/4) + i%4] = thisNote;
      note[8 * (i/4) + 7 - i%4] = thisNote;
    }
  }

  CNoteMap(byte chan)
  {
    m_chan = chan;
    init();    
  }
  void init()
  {

    m_lastNote = 0;
    byte scale[] = {
      0, 2, 3, 5, 7, 9, 10, 12    };
    randomize(40, scale, 8);
  }

  void stopLastNote()
  {
    if(m_lastNote)
    {
      midiWrite(MIDI_NOTE_ON|m_chan, m_lastNote, 0, 2);
      m_lastNote = 0;
    }
  }

  void event(byte row, byte col, CArpeggiator *pArpeggiator, CChordHolder *pChordHolder)
  {
    stopLastNote();
    m_lastNote = note[col + row*8];
    midiWrite(MIDI_NOTE_ON|m_chan, m_lastNote, 127, 2);     
    if(pArpeggiator)
      pArpeggiator->addNote(m_lastNote);
    if(pChordHolder)
      pChordHolder->addNote(m_lastNote);
  }  
};


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//
//
//                  G A M E   S T A T E   
//
//
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define ENDGAME_TICK 20        // ms between the 127 decrements of end of game CC
#define RALLYLEN_CC_SCALE 2    // how much to multiply number of bounces in rally into CC value 0-127

///////////////////////////////////////////////////////////////////////////////
// MIDI controller objects
CMetronome Metronome;
CNoteMap NoteMap1(CHAN_MONO1);
CNoteMap NoteMap2(CHAN_MONO2);
CChordHolder ChordHolder1(CHAN_POLY1);
CChordHolder ChordHolder2(CHAN_POLY2);
CArpeggiator Arpeggiator1(CHAN_ARP1);
CArpeggiator Arpeggiator2(CHAN_ARP2);
  
byte rallyInProgress;
byte lastPlayer;
byte endGame;
unsigned long lastBounceTime;
unsigned long averageBouncePeriod;
unsigned long nextEventTimeout;    

// reset everything ready for next rally
void initRally()
{    
  endGame = 0;
  rallyInProgress = 0;
  lastPlayer = 0;
  lastBounceTime = 0;
  averageBouncePeriod = 0;
  nextEventTimeout = 0;
  Metronome.init();
  NoteMap1.init();
  NoteMap2.init();
  ChordHolder1.init();
  ChordHolder2.init();
  Arpeggiator1.init();
  Arpeggiator2.init();
  initControllers();
  fireEvent(E_READY, 0);
}

// method called at the end of a rally
void endRally(unsigned long t)
{
  endGame = 127;
  nextEventTimeout = t + ENDGAME_TICK;
  setController(C_ENDGAME, 127);
}

// method called when there is a misread from the
// sensors
void misread(byte whichPlayer, unsigned long t)
{
  fireEvent(E_MISREAD, whichPlayer);
}

// this method is called when there is a 
// note event (knock on the table)
void bounce(byte whichPlayer, unsigned long t)
{
  // are we in the rally endgame?
  if(endGame)
  {
    // ignore events
    return;
  }
  // Have we registered that a rally is in progress
  else if(rallyInProgress)
  {
    // have we got two hits from the same player?
    if(whichPlayer == lastPlayer)
    {
      // droppped!
      setController(C_DROPPED, 1);
      fireEvent(E_DROPPED, whichPlayer);
      endRally(t);
    }
    // different player
    else
    {
      // rally is in progress!
      rallyInProgress++;

      // update the metronome pace        
      Metronome.event(t, whichPlayer);      

      // set bounce controllers
      setController(C_BOUNCE, 1);
      setController(C_DROPPED, 0);
      setController(C_RALLYLENMOD3, 64 * (rallyInProgress % 3));
      setController(C_RALLYLENMOD5, 32 * (rallyInProgress % 5));
      setController(C_RALLYLENMOD7, 22 * (rallyInProgress % 7));
      setController(C_RALLYLENMOD9, 16 * (rallyInProgress % 9));
      fireEvent(E_BOUNCE, whichPlayer);      
    }
  }
  else
  {
    // first bounce of the new rally
    fireEvent(E_FIRSTBOUNCE, whichPlayer);
    rallyInProgress = 1;      
    
    // start the metronome
    Metronome.start(t);
  }

  // timeout for next event
  lastBounceTime = t;
  if(!endGame)
  {
    nextEventTimeout = t + 3000;
    
    // rally tracking stuff
    setController(C_RALLYLEN, RALLYLEN_CC_SCALE * rallyInProgress);
    lastPlayer = whichPlayer;
  }
}  

// this method runs the game
void gameRun(unsigned long t)
{
  // run the metronome
  if(Metronome.run(t, endGame))
  {
    Arpeggiator1.run();
    Arpeggiator2.run();
  }
  
  if(endGame)
  {
    if(t > nextEventTimeout)
    {
      nextEventTimeout = t + ENDGAME_TICK;
      --endGame;
      setController(C_ENDGAME, endGame);
      if(!endGame)
        initRally();
        
    }
  }
  // is a rally in progress?
  else if(rallyInProgress)
  {
    // have we timed out the next event?
    if(t > nextEventTimeout)
    {
      // oh dear!
      setController(C_TIMEOUT, 1);
      fireEvent(E_TIMEOUT,0);
      endRally(t);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// INPUT RUN
// 
// main entry point for listening for information coming from the 
// MIDI input port
//
///////////////////////////////////////////////////////////////////////////////
void inputRun(unsigned long milliseconds)
{
  // we are only interest in note on messages
  byte msg = midiRead();
  if(MIDI_IS_NOTE_ON(msg) && midiParams[1] > 0)
  {    
     byte note = midiParams[0];
     byte row = note / 16;
     byte col = note % 16;
     
     switch(msg & 0x0f)
     {
       case 0:  // MIDI CHANNEL 1
         if(note  > 0x77)
         {
           setController(C_MISREAD, 1);
           misread(1,milliseconds);
         }
         else
         {
           byte dx = (col < 4) ? (3-col) : (col-4);
           setController(C_MISREAD, 0);
           setController(C_DX1, dx * 43);
           setController(C_X1, col * 19);
           setController(C_Y1, row * 19);
           bounce(1,milliseconds);
           NoteMap1.event(row, col, &Arpeggiator1, &ChordHolder1);
         }
         break;
       case 1:  // MIDI CHANNEL 2
         if(note > 0x77)
         {
           setController(C_MISREAD, 1);
           misread(2,milliseconds);
         }
         else
         {
           byte dx = (col < 4) ? (3-col) : (col-4);
           setController(C_MISREAD, 0);
           setController(C_DX2, dx * 43);
           setController(C_X2, col * 19);
           setController(C_Y2, row * 19);
           bounce(2,milliseconds);
           NoteMap2.event(row, col, &Arpeggiator2, &ChordHolder2);
         }
         break;
     }
  }
}

////////////////////////////////////////////////////////////////////////////////
//
//
//
// HEARTBEAT
//
//
//
////////////////////////////////////////////////////////////////////////////////
#define P_HEARTBEAT        13
#define HEARTBEAT_PERIOD 500
unsigned long heartbeatNext;
byte heartbeatStatus;

////////////////////////////////////////////////////////////////////////////////
// HEARTBEAT INIT
void heartbeatInit()
{
  pinMode(P_HEARTBEAT, OUTPUT);     
  heartbeatNext = 0;
  heartbeatStatus = 0;
}

////////////////////////////////////////////////////////////////////////////////
// HEARTBEAT RUN
void heartbeatRun(unsigned long milliseconds)
{
  if(milliseconds > heartbeatNext)
  {
    digitalWrite(P_HEARTBEAT, heartbeatStatus);
    heartbeatStatus = !heartbeatStatus;
    heartbeatNext = milliseconds + HEARTBEAT_PERIOD;    
  }
}

////////////////////////////////////////////////////////////////////////////////
//
//
// SETUP
//
//
////////////////////////////////////////////////////////////////////////////////
unsigned long nextEventRun;
unsigned long nextControllerRun;
void setup() 
{                
  nextEventRun = 0;
  nextControllerRun = 0;
  midiInit();
  heartbeatInit();
  initControllers();
}

////////////////////////////////////////////////////////////////////////////////
//
//
// SETUP
//
//
////////////////////////////////////////////////////////////////////////////////
void loop() 
{
  unsigned long milliseconds = millis();
  
  // heartbeat
  heartbeatRun(milliseconds);
  
  // run input
  inputRun(milliseconds);
  
  // run game functions
  gameRun(milliseconds);

  // manage the controllers
  if(milliseconds > nextControllerRun)
  {
    nextControllerRun = milliseconds + 10;
    runControllers();
  }        
  
  // manage the events
  if(milliseconds > nextEventRun)
  {
    runEvents();
    nextEventRun = milliseconds + EVENTS_POLL_DELAY;
  }
  
}

//
// EOF
//


