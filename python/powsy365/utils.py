# powsy365/utils.py

import numpy as np
import pandas as pd

def polar_to_rect(mag, ang_deg):
    ang_rad = np.deg2rad(ang_deg)
    return mag * np.cos(ang_rad) + 1j * mag * np.sin(ang_rad)

def rect_to_polar(z):
    mag = np.abs(z)
    ang_deg = np.angle(z, deg=True)
    return mag, ang_deg

def load_ieee_case(case_name):
    # Placeholder for loading IEEE test cases
    if case_name == "14":
        # Load IEEE 14 bus data
        pass
    return Network()

def plot_system(network):
    # Placeholder for plotting
    import matplotlib.pyplot as plt
    # Plot buses and lines
    pass

def export_results(results, filename):
    # Export analysis results to file
    pass