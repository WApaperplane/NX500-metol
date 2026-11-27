"""Pure host model: no device access, process launch, or persistent image writes."""
from dataclasses import dataclass, field

@dataclass
class Capture:
    ready: bool = False
    zram_owned: bool = False
    led_owned: bool = False
    header_allowed: bool = False
    events: list = field(default_factory=list)

    def run(self, preparation_ok: bool, header_ok: bool):
        self.events.append("preparation-check")
        if not preparation_ok:
            self.ready = True
            self.events.append("ready")
            return self
        self.zram_owned = True
        self.events.extend(("zram-acquired", "header-check"))
        if not header_ok:
            self.zram_owned = False
            self.events.extend(("zram-released", "ready"))
            self.ready = True
            return self
        self.led_owned = True
        self.header_allowed = True
        self.events.append("led-acquired")
        return self
