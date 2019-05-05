#include "esphome.h"
using namespace esphome;

class HD41Device : public UARTDevice {
  protected:
    // Physical Ports
    enum Port {
      // Invalid Port
      InvalidPort = -1,
      // Output
      OutputPort,
      // Input 1
      Input1Port,
      // Input 2
      Input2Port,
      // Input 3
      Input3Port,
      // Input 4
      Input4Port,
      // (internal use only)
      MaxPort_
    };

    // Source for Video output
    enum Source {
      // Invalid Port
      InvalidSource = -1,
      // Input 1
      Input1Source,
      // Input 2
      Input2Source,
      // Input 3
      Input3Source,
      // Input 4
      Input4Source,
      // (internal use only)
      MaxSource_
    };

    // Source for SPDIF output
    enum Edid {
      // Invalid Audio Profile
      InvalidEdid = -1,
      // Autoselect from EDID
      AutoEdid,
      // Stereo Audio 2.0
      StereoEdid,
      // Dolby/DTS 5.1
      DolbyDtsEdid,
      // HD Audio 7.1
      HdAudioEdid,
      // (internal use only)
      MaxEdid_
    };

    // Supported RS232 HEX Commands
    enum Command {
      // Switch to Input 1
      SetSourceInput1 = 0,
      // Switch to Input 2
      SetSourceInput2,
      // Switch to Input 3
      SetSourceInput3,
      // Switch to Input 4
      SetSourceInput4,
      // Query selected source
      GetSource,
      // Query connection state on Output
      IsOutputConnected,
      // Query connection state on Input 1
      IsInput1Connected,
      // Query connection state on Input 2
      IsInput2Connected,
      // Query connection state on Input 3
      IsInput3Connected,
      // Query connection state on Input 4
      IsInput4Connected,
      // Select audio profile from EDID
      SetEdidAuto,
      // Activate Stereo (2.0) decoding
      SetEdidStereo,
      // Activate Dolby/DTS (5.1) decoding
      SetEdidDolbyDts,
      // Activate HD Audio (7.1) decoding
      SetEdidHdAudio,
      // Query selected EDID
      GetEdid,
      // Enable automatic source switching
      EnableAuto,
      // Disable automatic source switching
      DisableAuto,
      // Query Auto setting
      GetAuto,
      // Enable ARC
      EnableArc,
      // Disable ARC
      DisableArc,
      // Query ARC setting
      GetArc,
      // (internal use only)
      MaxCommand_
    };

    HD41Device(UARTComponent *parent) : UARTDevice(parent) {}

    bool set_source(Source source) {
      assert(source >= 0 && source < Source::MaxSource_);

      send_command(static_cast<Command>(Command::SetSourceInput1 + source));
      return true;
    }

    Source get_source(bool &ok) {
      if (!send_command(Command::GetSource)) {
        ok = false;
        return Source::InvalidSource;
      }

      auto source = static_cast<Source>(buffer[6] - 1);
      if (source < Source::Input1Source || source > Source::Input4Source) {
        ESP_LOGE("hd41ctrl", "get_source: invalid response from device (0x%02x)", buffer[6]);
        ok = false;
        return Source::InvalidSource;
      }

      ok = true;
      return source;
    }

    bool is_port_connected(Port port, bool &ok) {
      assert(port >= 0 && port < Port::MaxPort_);

      if (!send_command(static_cast<Command>(Command::IsOutputConnected + port))) {
        ok = false;
        return false;
      }

      auto state = buffer[6];
      if (state != 0x00 && state != 0xff) {
        ESP_LOGE("hd41ctrl", "is_port_connected: invalid response from device (0x%02x)", buffer[6]);
        ok = false;
        return false;
      }

      ok = true;
      return state == 0x00;
    }

    bool set_edid(Edid edid) {
      assert(edid >= 0 && edid <= Edid::MaxEdid_);

      send_command(static_cast<Command>(Command::SetEdidAuto + edid));
      return true;
    }

    Edid get_edid(bool &ok) {
      if (!send_command(Command::GetEdid)) {
        ok = false;
        return Edid::InvalidEdid;
      }

      auto edid = static_cast<Edid>(buffer[6] - 1);
      if (edid < Edid::AutoEdid || edid > Edid::HdAudioEdid) {
        ESP_LOGE("hd41ctrl", "get_edid: invalid response from device (0x%02d)", buffer[6]);
        ok = false;
        return Edid::InvalidEdid;
      }

      return edid;
    }

    bool set_auto(bool enabled) {
      send_command(enabled ? Command::EnableAuto : Command::DisableAuto);
      return true;
    }

    bool get_auto(bool &ok) {
      if (!send_command(Command::GetAuto)) {
        ok = false;
        return false;
      }

      uint8_t state = buffer[4];
      if (state != 0xf0 && state != 0x0f) {
        ESP_LOGE("hd41ctrl", "get_auto: invalid response from device (0x%02x)", buffer[4]);
        ok = false;
        return false;
      }

      ok = true;
      return state == 0x0f;
    }

    bool set_arc(bool enabled) {
      send_command(enabled ? Command::EnableArc : Command::DisableArc);
      return true;
    }

    bool get_arc(bool &ok) {
      if (!send_command(Command::GetArc)) {
        ok = false;
        return false;
      }

      uint8_t state = buffer[4];
      if (state != 0xf0 && state != 0x0f) {
        ESP_LOGE("hd41ctrl", "get_arc: invalid response from device (0x%02x)", buffer[4]);
        ok = false;
        return false;
      }

      ok = true;
      return state == 0x0f;
    }

    bool send_command(Command cmd) {
      assert(cmd >= 0 && cmd <= Command::MaxCommand_);

      // Sometimes commands are not recognised by the unit due to
      // internal processes (eg. when it is busy switching). This leads
      // to invalid/corrupted responses. Luckily the vendor includes a
      // simple checksum in the responses which can identify invalid
      // responses.
      //
      // If a command fails it is repeated up to three times.

      // Retry loop
      bool ok = false;
      for (int attempt = 1; !ok && attempt <= 3; attempt++) {
        // Send command to device
        write_array(vorke_command[cmd], sizeof(vorke_command[cmd]));

        // Wait for response to become available
        auto start_time = millis();
        while (available() != sizeof(buffer)) {
          if (millis() - start_time > 1000) {
            break;
          }

          // yield so the watchdog doesn't trigger
          yield();
        }

        if (available() == sizeof(buffer)) {
          read_array(buffer, sizeof(buffer));

          // For the checksum add up all byte and the result must be 0x00
          uint8_t checksum = 0;
          for (int i=0; i<sizeof(buffer); i++) {
            checksum += buffer[i];
          }

          if (checksum == 0x00) {
            ok = true;
            continue;
          }

          ESP_LOGW("hd41ctrl", "send_command(0x%02x): invalid checksum - discarding buffer", cmd);
        }

        // Getting to this point means the read failed
        ESP_LOGW("hd41ctrl", "send_command(0x%02x): invalid or no response - retrying (attempt %d)", cmd, attempt);

        // Empty input buffer for next response
        if (available() > 0) {
          auto count = available();
          ESP_LOGW("hd41ctrl", "send_command(0x%02x): discarding input buffer (%d bytes)", cmd, count);

          // read it empty...
          for (uint8_t dummy = 0; count > 0; count--) {
            read_byte(&dummy);
          }
        }
      }

      // If ok flag has not been set all attempts failed
      if (!ok) {
        ESP_LOGE("hd41ctrl", "send_command: unit not responding to command - giving up");
        return false;
      }

      return true;
    }

  private:
    uint8_t buffer[13];

    constexpr static const uint8_t vorke_command[Command::MaxCommand_][13] = {
      { 0xa5, 0x5b, 0x02, 0x03, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf9 }, // Select Input 1
      { 0xa5, 0x5b, 0x02, 0x03, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8 }, // Select Input 2
      { 0xa5, 0x5b, 0x02, 0x03, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf7 }, // Select Input 3
      { 0xa5, 0x5b, 0x02, 0x03, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6 }, // Select Input 4
      { 0xa5, 0x5b, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc }, // Query Selected Input
      { 0xa5, 0x5b, 0x01, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf9 }, // Query Conn State Output
      { 0xa5, 0x5b, 0x01, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa }, // Query Conn State Input 1
      { 0xa5, 0x5b, 0x01, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf9 }, // Query Conn State Input 2
      { 0xa5, 0x5b, 0x01, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8 }, // Query Conn State Input 3
      { 0xa5, 0x5b, 0x01, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf7 }, // Query Conn State Input 4
      { 0xa5, 0x5b, 0x03, 0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf9 }, // Select Audio Auto
      { 0xa5, 0x5b, 0x03, 0x02, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8 }, // Select Audio Stereo Audio 2.0
      { 0xa5, 0x5b, 0x03, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf7 }, // Select Audio Dolby/DTS 5.1
      { 0xa5, 0x5b, 0x03, 0x02, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6 }, // Select Audio HD Audio 7.1
      { 0xa5, 0x5b, 0x01, 0x0c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2 }, // Query Selected Audio
      { 0xa5, 0x5b, 0x02, 0x05, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea }, // AutoSwitch On
      { 0xa5, 0x5b, 0x02, 0x05, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09 }, // AutoSwitch Off
      { 0xa5, 0x5b, 0x01, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2 }, // Query AutoSwitch
      { 0xa5, 0x5b, 0x10, 0x01, 0x0f, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdf }, // ARC On
      { 0xa5, 0x5b, 0x10, 0x01, 0xf0, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe }, // ARC Off
      { 0xa5, 0x5b, 0x10, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xed }, // Query ARC
    };
};

constexpr const uint8_t HD41Device::vorke_command[][13];

#ifdef USE_SWITCH

class HD41Switch : public HD41Device, public PollingComponent {
  public:
    // Available logical switches
    enum Switches {
      // ARC on/off
      ArcSwitch = 0,
      // Auto on/off
      AutoSourceSwitch,

      // Select Input 1
      Input1Switch,
      // Select Input 2
      Input2Switch,
      // Select Input 3
      Input3Switch,
      // Select Input 4
      Input4Switch,

      // Select Auto EDID
      AutoEdidSwitch,
      // Select Stereo (2.0) EDID
      StereoSwitch,
      // Select Dolby/DTS (5.1) EDID
      DolbyDtsSwitch,
      // Select HD Audio (7.1) EDID
      HdAudioSwitch,

      // Number of switches
      MaxSwitch_
    };

    class DummySwitch : public Component, public switch_::Switch {
      public:
        void write_state(bool state) override {
          publish_state(state);
        }
    };

  public:
    HD41Switch(UARTComponent *parent) : HD41Device(parent), PollingComponent(1000) {
      memset(switches, 0, sizeof(switches));
    }

    DummySwitch *make_switch(Switches sw) {
      assert(sw >= 0 && sw < Switches::MaxSwitch_);

      if (switches[sw] == 0) {
        switches[sw] = new DummySwitch();
        switches[sw]->add_on_state_callback([this, sw](bool state) {
          if (sw == Switches::ArcSwitch && !updating) {
            // ARC switch (bool)
            set_arc(state);
          } else if (sw == Switches::AutoSourceSwitch && !updating) {
            // AUTO switch (bool)
            set_auto(state);
          } else if (state) {
            if (sw >= Switches::Input1Switch && sw <= Switches::Input4Switch) {
              // Video select (mutual exclusive)

              // Iterate all switches in this group
              for (int sw_check = Switches::Input1Switch; sw_check <= Switches::Input4Switch; sw_check++) {
                if (sw_check == sw) {
                  // This is the switch which triggered the callback, so
                  // send the command for switching to the unit.
                  //
                  // If this has been triggered by update() then nothing is
                  // sent to avoid a feedback loop.
                  if (!updating) {
                    set_source(static_cast<Source>(Source::Input1Source + (sw_check - Switches::Input1Switch)));
                  }
                } else if (switches[sw_check] != 0) {
                  // All other switches will be reset to false
                  switches[sw_check]->publish_state(false);
                }
              }
            } else if (sw >= Switches::AutoEdidSwitch && sw <= Switches::HdAudioSwitch) {
              // Audio select (mutual exclusive) - works like Video select
              for (int sw_check = Switches::AutoEdidSwitch; sw_check <= Switches::HdAudioSwitch; sw_check++) {
                if (sw_check == sw) {
                  if (!updating) {
                    set_edid(static_cast<Edid>(Edid::AutoEdid + (sw_check - Switches::AutoEdidSwitch)));
                  }
                } else if (switches[sw_check] != 0) {
                  switches[sw_check]->publish_state(false);
                }
              }
           }
          }
        });

        // Enable groups to minimize RS232 communication if not all
        // switches are requested though make_switch().
        arc_enabled   = arc_enabled   || (sw == Switches::ArcSwitch);
        scan_enabled  = scan_enabled  || (sw == Switches::AutoSourceSwitch);
        input_enabled = input_enabled || (sw >= Switches::Input1Switch && sw <= Switches::Input4Switch);
        audio_enabled = audio_enabled || (sw >= Switches::AutoEdidSwitch && sw <= Switches::HdAudioSwitch);
      }

      return switches[sw];
    }

    void update() override {
      // Set updating flag so updates do not trigger a set_*() in the lambda functions
      updating = true;

      bool ok = false;
      for (int sw = 0; sw < Switches::MaxSwitch_; sw++) {
        if (switches[sw]) {
          if (arc_enabled && sw == Switches::ArcSwitch) {
            // ARC switch
            bool state = get_arc(ok);
            if (ok) {
              switches[sw]->publish_state(state);
            }
          } else if (scan_enabled && sw == Switches::AutoSourceSwitch) {
            // Auto switch
            bool state = get_auto(ok);
            if (ok) {
              switches[sw]->publish_state(state);
            }
          } else if (input_enabled && sw >= Switches::Input1Switch && sw <= Switches::Input4Switch) {
            // Source switch
            auto selected = static_cast<Switches>(get_source(ok) - Source::Input1Source + Switches::Input1Switch);
            if (ok) {
              for (int sw_check = Switches::Input1Switch; sw_check <= Switches::Input4Switch; sw_check++) {
                if (switches[sw_check] != 0) {
                  switches[sw_check]->publish_state(sw_check == selected);
                }
              }
            }
          } else if (audio_enabled && sw >= Switches::AutoEdidSwitch && sw <= Switches::HdAudioSwitch) {
            // EDID switch
            auto selected = static_cast<Switches>(get_edid(ok) - Edid::AutoEdid + Switches::AutoEdidSwitch);
            if (ok) {
              for (int sw_check = Switches::AutoEdidSwitch; sw_check <= Switches::HdAudioSwitch; sw_check++) {
                if (switches[sw_check] != 0) {
                  switches[sw_check]->publish_state(sw_check == selected);
                }
              }
            }
          }
        }
      }

      // release update lock
      updating = false;
    }

  public:
    DummySwitch *switches[Switches::MaxSwitch_];

    bool updating = false;
    bool arc_enabled = false;
    bool scan_enabled = false;
    bool input_enabled = false;
    bool audio_enabled = false;
};

#endif

#ifdef USE_BINARY_SENSOR

class HD41BinarySensor : public HD41Device, public PollingComponent, public binary_sensor::BinarySensor {
  public:
    // Available logical switches
    enum Type {
      // Output Port
      OutputConnected = 0,
      // Input 1 Port
      Input1Connected,
      // Input 2 Port
      Input2Connected,
      // Input 3 Port
      Input3Connected,
      // Input 4 Port
      Input4Connected,
      // Number of types (internal use)
      MaxType_
    };

    HD41BinarySensor(UARTComponent *parent) : HD41Device(parent), PollingComponent(5000) {
      memset(sensors, 0, sizeof(sensors));
    }

    binary_sensor::BinarySensor *make_sensor(Type type) {
      assert(type >= 0 && type < Type::MaxType_);

      if (sensors[type] == 0) {
        sensors[type] = new binary_sensor::BinarySensor();
      }

      return sensors[type];
    }

    void update() override {
      bool result, ok;
      for (int type = 0; type < Type::MaxType_; type++) {
        if (sensors[type] != 0) {
          result = is_port_connected(static_cast<Port>(Port::OutputPort + type), ok);
          if (ok) {
            sensors[type]->publish_state(result);
          }
        }
      }
    }

  private:
    binary_sensor::BinarySensor *sensors[Type::MaxType_];
};

#endif