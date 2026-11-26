# @name Model Y Simulator
# @description Sends Model 3/Y vehicle CAN frames (drive cycle) on one bus and checks reception on the other
# @bus 0
#
# Setup: connect CAN-A (bus 0, MCP2518FD) H/L/SGND to CAN-B (bus 1, TWAI) H/L/SGND.
# The 120 ohm terminations RZ1/RZ2 are fixed on the board -> ~60 ohm combined.
# Bus 0 transmits, bus 1 receives and counts. Docs: docs/simulator.md
#
# DBC: signals by name (can_msg_new / msg_sig_set / msg_sig_get) from
#   dbc/Model3_VEH.dbc (bus 0 = vehicle CAN, with Model3_CH.dbc as in the web UI).
#   Names are resolved to bit positions on upload (.bep):
#   web UI "Save" or tools/upload_script.py (uses the same preprocessor).
#   Cross-checked against joshwardell/model3dbc, commaai/opendbc tesla_model3_party/vehicle.
#   Juniper (Model Y 2025+): IDs/layouts taken from Model 3/Y, not verified on
#   Juniper -> docs/simulator.md, section "DBC and Model Y Juniper".
# Checksum (byte 0): (ID_lo + ID_hi + sum of bytes 1..7) & 0xFF, like
#   firmware/src/can/tesla.c and the opendbc tesla checksum.
#
# Without preprocessing (raw file on the device) the script does NOT run:
# msg_sig_set then needs numeric arguments.

# ---------------- Configuration ----------------
var mys_cfg = {
  'enabled':   true,    # false = script loads but sends nothing
  'tx_bus':    0,       # 0 = CAN-A (MCP2518FD), 1 = CAN-B (TWAI)
  'rx_bus':    1,       # check bus; -1 = no RX check
  'speed':     1.0,     # drive cycle time scale (2.0 = twice as fast)
  'tick_ms':   20,      # base tick; >= 20 because of the firmware rate limit (10 ms/ID)
  'report_s':  10,      # interval of the RX summary in the log
  'log_phase': true,    # log phase changes
  'throttle':  true,    # no RX: send only 1 frame/s per ID
}

# ID -> period in ms (source). 10 ms frames run at 20 ms (rate limit).
var mys_msgs = [
  [0x118, 20],    # DI_systemStatus: gear, brake, accel pedal (JW 10 ms)
  [0x129, 20],    # SCCM_steeringAngleSensor: steering angle  (JW 10 ms)
  [0x257, 20],    # DI_speed: vehicle speed                    (JW 20 ms)
  [0x39D, 40],    # IBST_status: brake pedal                   (estimated)
  [0x292, 100],   # BMS_socStatus: state of charge             (JW 100 ms)
  [0x3F5, 100],   # VCFRONT_lighting: indicators, hazards      (estimated)
  [0x102, 100],   # VCLEFT_doorStatus: left doors              (estimated)
  [0x103, 100],   # VCRIGHT_doorStatus: right doors, trunk     (estimated)
]

# IDs with a 4-bit counter: here rx/tx is a real frame count,
# the others are only checked for "arrives and content matches".
var mys_ctr = {0x118: true, 0x129: true, 0x257: true, 0x39D: true}

# Drive cycle: [name, duration s]
var mys_phases = [
  ['park',    8],   # P, driver door open 3-6 s
  ['ready',   2],   # press brake, P -> D
  ['accel',  10],   # 0 -> 100 km/h, left indicator 0-3 s
  ['cruise', 15],   # 100 km/h, lane change right 5-8 s
  ['brake',   8],   # 100 -> 0 km/h
  ['reverse', 6],   # R, parking with steering lock
  ['park2',   6],   # P, trunk open 1-5 s
]

class MYSim
  var cfg, tx, rx, ticks, t, phase, pt, cnt, txc, txlast, rxlast, rxupd, rxseen, rxok
  var last_rx_ms, throttled, h_tick, h_rep, soc, v, gear, pedal, brake, acc
  var steer, steer_rate, blink_l, blink_r, door_fl, door_fr, door_rl, door_rr, trunk

  def init(cfg)
    self.cfg = cfg
    self.tx = cfg['tx_bus']
    self.rx = cfg['rx_bus']
    self.ticks = 0
    self.t = 0.0
    self.phase = 0
    self.pt = 0.0
    self.cnt = {}
    self.txc = {}
    self.txlast = {}
    self.rxlast = {}
    self.rxupd = {}
    self.rxseen = {}
    self.rxok = {}
    self.last_rx_ms = millis()
    self.throttled = false
    self.soc = 80.0
    for m: mys_msgs
      self.cnt[m[0]] = 0
      self.txc[m[0]] = 0
      self.rxupd[m[0]] = 0
      self.rxseen[m[0]] = false
      self.rxok[m[0]] = 0
    end
    self.drive_state()
  end

  # ---------- Vehicle state from phase + phase time ----------
  def drive_state()
    var name = mys_phases[self.phase][0]
    var p = self.pt
    self.v = 0.0  self.gear = 1  self.pedal = 0.0  self.brake = false
    self.acc = 0.0  self.steer = 0.0  self.steer_rate = 0.0
    self.blink_l = false  self.blink_r = false
    self.door_fl = false  self.door_fr = false  self.door_rl = false  self.door_rr = false
    self.trunk = false
    if name == 'park'
      self.door_fl = (p >= 3 && p < 6)
    elif name == 'ready'
      self.brake = true
      self.gear = p < 1 ? 1 : 4
    elif name == 'accel'
      self.gear = 4  self.v = p * 10.0  self.pedal = 40.0  self.acc = 2.78
      self.blink_l = p < 3
      if p < 3 self.steer = -5.0 * p end
    elif name == 'cruise'
      self.gear = 4  self.v = 100.0  self.pedal = 15.0
      self.blink_r = (p >= 5 && p < 8)
      if p >= 5 && p < 8 self.steer = 3.0 end
    elif name == 'brake'
      self.gear = 4  self.v = 100.0 - p * 12.5  self.brake = true  self.acc = -3.47
      if self.v < 0 self.v = 0.0 end
    elif name == 'reverse'
      self.gear = 2
      if p < 1 self.brake = true
      elif p < 3 self.v = (p - 1) * 2.5  self.pedal = 8.0  self.steer = (p - 1) * 200.0  self.steer_rate = 200.0
      elif p < 5 self.v = 5.0 - (p - 3) * 2.5  self.pedal = 4.0  self.steer = 400.0 - (p - 3) * 200.0  self.steer_rate = -200.0
      else self.brake = true
      end
    elif name == 'park2'
      self.trunk = (p >= 1 && p < 5)
    end
  end

  # ---------- Frame helpers ----------
  # Signals are set by DBC name (msg_sig_set). The web UI
  # preprocessor (or tools/upload_script.py) resolves the names on upload
  # against dbc/Model3_VEH.dbc (+ Model3_CH.dbc) to start bit/length/factor
  # (.bep). There is no DBC runtime dependency on the device.
  def next_ctr(id)
    var c = self.cnt[id]
    self.cnt[id] = (c + 1) % 16
    return c
  end

  # Tesla checksum byte 0: (ID_lo + ID_hi + sum of bytes 1..7) & 0xFF
  # (firmware/src/can/tesla.c, opendbc tesla checksum). Not part of the DBC.
  def checksum(m)
    var d = m['data']
    var id = m['id']
    var s = (id & 0xFF) + ((id >> 8) & 0xFF)
    var n = size(d) - 1
    for i: 1..n s += d[i] end
    d[0] = s & 0xFF
    return m
  end

  def blink_phase()   # indicator blink ~1.5 Hz: 2 = ACTIVE_HIGH, 1 = ACTIVE_LOW (VEH VAL_)
    return ((millis() / 333) % 2) == 0 ? 2 : 1
  end

  def build(id)
    if id == 0x118
      var ss = can_msg_new("DI_systemStatus")
      msg_sig_set(ss, "DI_immobilizerState", 3)          # DISARMED
      msg_sig_set(ss, "DI_systemState", self.gear == 1 ? 2 : 5)   # STANDBY / ENABLE
      msg_sig_set(ss, "DI_brakePedalState", self.brake ? 1 : 0)
      msg_sig_set(ss, "DI_gear", self.gear)              # 1 P, 2 R, 3 N, 4 D
      msg_sig_set(ss, "DI_hvilSystemStatus", 2)          # CLOSED
      msg_sig_set(ss, "DI_accelPedalPos", self.pedal)    # %
      msg_sig_set(ss, "DI_vehicleAcceleration", self.acc)   # m/s2, signed
      msg_sig_set(ss, "DI_systemStatusCounter", self.next_ctr(id))
      return self.checksum(ss)
    elif id == 0x129
      var sa = can_msg_new("SCCM_steeringAngleSensor")
      msg_sig_set(sa, "SCCM_steeringAngleSensorStatus", 0)   # OK
      msg_sig_set(sa, "SCCM_steeringAngle", self.steer)       # degrees
      msg_sig_set(sa, "SCCM_steeringAngleValidity", 1)       # VALID
      msg_sig_set(sa, "SCCM_steeringAngleSpeed", self.steer_rate)
      msg_sig_set(sa, "SCCM_steeringAngleCounter", self.next_ctr(id))
      # Byte 0 is called "Crc" here - algorithm unconfirmed, using the sum checksum
      return self.checksum(sa)
    elif id == 0x257
      var sp = can_msg_new("DI_speed")
      msg_sig_set(sp, "DI_vehicleSpeed", self.v)              # 43|13 (VEH/joshwardell)
      msg_sig_set(sp, "DI_opdVehicleModelSpeedRef", self.v)   # 12|12 = opendbc "DI_vehicleSpeed"
      msg_sig_set(sp, "DI_uiSpeed", int(self.v + 0.5))
      msg_sig_set(sp, "DI_uiSpeedUnits", 1)                   # KPH
      msg_sig_set(sp, "DI_accelPedalPressed", self.pedal > 0 ? 1 : 0)
      msg_sig_set(sp, "DI_velocityEstimatorState", 1)
      msg_sig_set(sp, "DI_speedCounter", self.next_ctr(id))
      return self.checksum(sp)
    elif id == 0x39D
      var ib = can_msg_new("IBST_status")
      msg_sig_set(ib, "IBST_iBoosterStatus", self.brake ? 6 : 5)    # ACTUATION / READY
      msg_sig_set(ib, "IBST_driverBrakeApply", self.brake ? 2 : 1)  # APPLYING / NOT_APPLIED
      msg_sig_set(ib, "IBST_sInputRodDriver", self.brake ? 12.0 : 0.0)   # mm
      msg_sig_set(ib, "IBST_statusCounter", self.next_ctr(id))
      return self.checksum(ib)
    elif id == 0x292
      var bm = can_msg_new("BMS_socStatus")                   # no counter/checksum
      msg_sig_set(bm, "BMS_socMin", self.soc - 0.5)
      msg_sig_set(bm, "BMS_socUI", self.soc)
      msg_sig_set(bm, "BMS_socMax", self.soc + 0.5)
      msg_sig_set(bm, "BMS_socAvg", self.soc)
      msg_sig_set(bm, "BMS_enoughEnergyForConvenienceFeatures", 1)
      return bm
    elif id == 0x3F5
      var ph = self.blink_phase()
      var l = self.blink_l ? ph : 0
      var r = self.blink_r ? ph : 0
      var li = can_msg_new("VCFRONT_lighting")                # no counter
      msg_sig_set(li, "VCFRONT_indicatorLeftRequest", l)
      msg_sig_set(li, "VCFRONT_indicatorRightRequest", r)
      msg_sig_set(li, "VCFRONT_lightingCoreState", self.gear == 1 ? 1 : 2)
      msg_sig_set(li, "VCFRONT_turnIndicatorStalk", self.blink_l ? 1 : (self.blink_r ? 2 : 0))
      msg_sig_set(li, "VCFRONT_indicatorLeftInternal", l)
      msg_sig_set(li, "VCFRONT_indicatorRightInternal", r)
      return li
    elif id == 0x102
      var dl = can_msg_new("VCLEFT_doorStatus")               # 1 OPENED, 2 CLOSED
      msg_sig_set(dl, "VCLEFT_doorClosureStatusFront", self.door_fl ? 1 : 2)
      msg_sig_set(dl, "VCLEFT_doorClosureStatusRear", self.door_rl ? 1 : 2)
      msg_sig_set(dl, "VCLEFT_frontLatchStatus", self.door_fl ? 1 : 2)
      msg_sig_set(dl, "VCLEFT_rearLatchStatus", self.door_rl ? 1 : 2)
      return dl
    elif id == 0x103
      var dr = can_msg_new("VCRIGHT_doorStatus")
      msg_sig_set(dr, "VCRIGHT_doorClosureStatusFront", self.door_fr ? 1 : 2)
      msg_sig_set(dr, "VCRIGHT_doorClosureStatusRear", self.door_rr ? 1 : 2)
      msg_sig_set(dr, "VCRIGHT_frontLatchStatus", self.door_fr ? 1 : 2)
      msg_sig_set(dr, "VCRIGHT_rearLatchStatus", self.door_rr ? 1 : 2)
      msg_sig_set(dr, "VCRIGHT_trunkLatchStatus", self.trunk ? 1 : 2)
      return dr
    end
    return nil
  end

  # Decode the RX side by signal (same DBC) - for the report.
  def rx_decode()
    if self.rx < 0 return '' end
    var sp = can_msg_get(self.rx, "DI_speed")
    var ss = can_msg_get(self.rx, "DI_systemStatus")
    var bm = can_msg_get(self.rx, "BMS_socStatus")
    var out = ' | rx-decoded:'
    if sp != nil out += format(' v=%.1f', msg_sig_get(sp, "DI_vehicleSpeed")) end
    if ss != nil out += ' gear=' .. 'xPRND'[int(msg_sig_get(ss, "DI_gear")) % 5] end
    if bm != nil out += format(' soc=%.1f', msg_sig_get(bm, "BMS_socUI")) end
    return out
  end

  # ---------- RX check ----------
  def rx_poll()
    if self.rx < 0 return end
    var now = millis()
    for m: mys_msgs
      var id = m[0]
      var d = can_recv_raw(self.rx, id, 0)
      if d != nil
        # The cache always holds the last frame: "new" can only be detected by
        # changed data. Frames with a counter change on every send.
        var prev = self.rxlast.find(id)
        var t = self.txlast.find(id)
        var alive = now - self.last_rx_ms < 500
        if prev == nil || !(prev == d)
          self.rxupd[id] += 1
          self.rxlast[id] = d
          self.rxseen[id] = true
          if mys_ctr.find(id) != nil self.last_rx_ms = now end
        elif alive && t != nil && t == d
          self.rxseen[id] = true
        end
        if t != nil && t == d self.rxok[id] += 1 end
      end
    end
  end

  def report()
    var tot_tx = 0  var tot_rx = 0  var miss = []  var bad = []  var lossy = []
    var line = ''
    for m: mys_msgs
      var id = m[0]
      tot_tx += self.txc[id]
      tot_rx += self.rxupd[id]
      if mys_ctr.find(id) != nil && self.txc[id] > 0 && self.rxupd[id] * 10 < self.txc[id] * 9
        lossy.push(format('%03X', id))
      end
      if !self.rxseen[id] miss.push(format('%03X', id))
      elif self.rxok[id] == 0 bad.push(format('%03X', id))
      end
      if mys_ctr.find(id) != nil
        line += format(' %03X:%d/%d', id, self.rxupd[id], self.txc[id])
      else
        line += format(' %03X:%s', id, self.rxseen[id] ? 'ok' : '--')
      end
    end
    var ph = mys_phases[self.phase][0]
    print(format('SIM %s v=%.0f gear=%s soc=%.1f | tx=%d rx=%d', ph, self.v,
          'xPRND'[self.gear], self.soc, tot_tx, tot_rx) .. self.rx_decode())
    if self.rx >= 0
      print('SIM rx/tx (counter IDs) / ok' .. line)
      if size(lossy) > 0 && size(miss) == 0
        print('SIM losses >10 %: ' .. lossy.concat(' ') .. ' (error frames? check termination)')
      end
      if size(bad) > 0
        print('SIM data mismatch: ' .. bad.concat(' '))
      end
      if size(miss) > 0
        print('SIM MISSING on bus' .. str(self.rx) .. ': ' .. miss.concat(' ') ..
              ' -> check H/L wiring, 60 ohm, bitrate')
      else
        print('SIM all ' .. str(size(mys_msgs)) .. ' IDs received' ..
              (self.throttled ? ' (throttled)' : ''))
      end
    end
    for m: mys_msgs self.txc[m[0]] = 0  self.rxupd[m[0]] = 0  self.rxseen[m[0]] = false  self.rxok[m[0]] = 0 end
  end

  # ---------- Tick ----------
  def tick()
    var dt = self.cfg['tick_ms'] / 1000.0 * self.cfg['speed']
    self.rx_poll()
    # advance the drive cycle
    self.pt += dt
    if self.pt >= mys_phases[self.phase][1]
      self.pt = 0.0
      self.phase = (self.phase + 1) % size(mys_phases)
      if self.cfg['log_phase'] print('SIM phase: ' .. mys_phases[self.phase][0]) end
    end
    self.drive_state()
    # SoC drops ~0.05 %/s at 100 km/h (deliberately fast so it is visible)
    if self.v > 0 self.soc -= dt * 0.05 * self.v / 100.0 end
    if self.soc < 10 self.soc = 80.0 end
    # Throttle when nothing arrives on the RX bus (avoids a full TX queue / 50 ms stalls)
    if self.cfg['throttle'] && self.rx >= 0
      var silent = millis() - self.last_rx_ms > 3000
      if silent != self.throttled
        self.throttled = silent
        print(silent ? 'SIM no RX on bus' .. str(self.rx) .. ' for 3 s -> throttling to 1 Hz'
                     : 'SIM RX back -> full rate')
      end
    end
    self.ticks += 1
    var ms = self.ticks * self.cfg['tick_ms']
    for m: mys_msgs
      var per = self.throttled ? 1000 : m[1]
      if ms % per == 0
        var msg = self.build(m[0])
        if msg != nil
          can_msg_send(self.tx, msg)
          self.txc[m[0]] += 1
          self.txlast[m[0]] = msg['data']
        end
      end
    end
  end

  def start()
    if self.rx >= 0
      if self.rx == self.tx print('SIM WARNING: rx_bus == tx_bus, own frames do not come back') end
      for m: mys_msgs can_recv_raw(self.rx, m[0], 0) end   # register cache slots
    end
    var me = self
    self.h_tick = timer_every(self.cfg['tick_ms'], def () me.tick() end)
    self.h_rep = timer_every(self.cfg['report_s'] * 1000, def () me.report() end)
    print(format('SIM start: tx bus%d -> rx bus%d, %d IDs, time scale %.1fx',
          self.tx, self.rx, size(mys_msgs), self.cfg['speed']))
  end

  def stop()
    if self.h_tick != nil timer_cancel(self.h_tick)  self.h_tick = nil end
    if self.h_rep != nil timer_cancel(self.h_rep)  self.h_rep = nil end
    print('SIM stopped')
  end
end

def setup()
  var sim = MYSim(mys_cfg)
  action_register('Sim Start/Stop', def ()
    if sim.h_tick == nil sim.start() else sim.stop() end
  end)
  action_register('Sim Report', def () sim.report() end)
  if mys_cfg['enabled'] sim.start() else print('SIM loaded, disabled (enabled=false)') end
end

def teardown()
  print('SIM finished')
end
