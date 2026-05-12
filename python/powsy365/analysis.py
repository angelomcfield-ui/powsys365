# powsy365/analysis.py

from .core import LoadFlowSolver
from .network import Network

class Analysis:
    def __init__(self, network):
        self.network = network
        self.system = network.get_system()

    def run_load_flow(self, method="NR", tolerance=1e-6, max_iter=100):
        solver = LoadFlowSolver(self.system, tolerance, max_iter, method)
        return solver.solve()

    def run_short_circuit(self):
        # Placeholder for short circuit analysis
        pass

    def run_stability(self):
        # Placeholder for stability analysis
        pass

    def run_opf(self):
        # Placeholder for optimal power flow
        pass