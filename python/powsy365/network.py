# powsy365/network.py

import pandas as pd
import numpy as np
from .core import PowerSystem, Bus, Line, Generator, Load

class Network:
    def __init__(self):
        self.system = PowerSystem()
        self.bus_data = pd.DataFrame()
        self.line_data = pd.DataFrame()
        self.gen_data = pd.DataFrame()
        self.load_data = pd.DataFrame()

    def add_bus(self, bus_number, name, v_base_kv, vm_pu=1.0, va_deg=0.0, bus_type=1):
        bus = Bus()
        bus.number = bus_number
        bus.name = name
        bus.v_base_kv = v_base_kv
        bus.vm_pu = vm_pu
        bus.va_deg = va_deg
        bus.type = bus_type
        self.system.add_bus(bus)

    def add_line(self, from_bus, to_bus, r_pu, x_pu, b_pu=0.0, length_km=0.0):
        line = Line()
        line.from_bus = from_bus
        line.to_bus = to_bus
        line.z_pu = complex(r_pu, x_pu)
        line.b_pu = b_pu
        self.system.add_line(line)

    def add_generator(self, bus, p_mw, q_mvar, p_max, p_min, q_max, q_min):
        gen = Generator()
        gen.bus = bus
        gen.p_mw = p_mw
        gen.q_mvar = q_mvar
        gen.p_max = p_max
        gen.p_min = p_min
        gen.q_max = q_max
        gen.q_min = q_min
        self.system.add_generator(gen)

    def add_load(self, bus, p_mw, q_mvar):
        load = Load()
        load.bus = bus
        load.p_mw = p_mw
        load.q_mvar = q_mvar
        self.system.add_load(load)

    def get_system(self):
        return self.system