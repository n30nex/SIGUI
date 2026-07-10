from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_slice(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start + len(signature))
    return source[start:end]


@dataclass
class ProbeGateModel:
    next_id: int = 0
    pending_id: int = 0
    inflight_id: int = 0
    completed_id: int = 0
    abandoned_id: int = 0
    signaled_id: int = 0
    admission_reserved: bool = False
    semaphore_token: int = 0

    def reserve_admission(self) -> bool:
        if self.admission_reserved or self.pending_id or self.inflight_id:
            return False
        self.admission_reserved = True
        return True

    def drain_stale_signal(self) -> None:
        assert self.admission_reserved
        self.semaphore_token = 0

    def commit_admission(self) -> int:
        if not self.admission_reserved or self.pending_id or self.inflight_id:
            return 0
        self.next_id = (self.next_id + 1) & 0xFFFFFFFF
        if self.next_id == 0:
            self.next_id = 1
        self.pending_id = self.next_id
        self.completed_id = 0
        self.abandoned_id = 0
        self.signaled_id = 0
        self.admission_reserved = False
        return self.pending_id

    def begin(self) -> int:
        if not self.pending_id or self.inflight_id:
            return 0
        request_id = self.pending_id
        self.pending_id = 0
        self.inflight_id = request_id
        return request_id

    def timeout(self, request_id: int) -> None:
        if self.pending_id == request_id:
            self.pending_id = 0
        elif self.inflight_id == request_id:
            if self.signaled_id == request_id:
                self.inflight_id = 0
                self.completed_id = 0
                self.abandoned_id = 0
                self.signaled_id = 0
            else:
                self.abandoned_id = request_id

    def publish_and_give(self, request_id: int) -> bool:
        if not request_id or self.inflight_id != request_id:
            return False
        self.completed_id = request_id
        self.semaphore_token = 1
        return True

    def finish_signal(self, request_id: int) -> None:
        if self.inflight_id != request_id:
            return
        self.signaled_id = request_id
        if self.abandoned_id == request_id:
            self.inflight_id = 0
            self.completed_id = 0
            self.abandoned_id = 0
            self.signaled_id = 0

    def consume_and_acknowledge(self, request_id: int) -> bool:
        if not self.semaphore_token:
            return False
        self.semaphore_token = 0
        if self.inflight_id != request_id or self.completed_id != request_id:
            return False
        self.completed_id = 0
        self.abandoned_id = 0
        self.signaled_id = 0
        if self.inflight_id == request_id:
            self.inflight_id = 0
        return True


def test_probe_gate_blocks_late_completion_from_becoming_the_next_result():
    gate = ProbeGateModel()
    assert gate.reserve_admission() is True
    gate.drain_stale_signal()
    request_a = gate.commit_admission()
    assert request_a == 1
    assert gate.begin() == request_a

    gate.timeout(request_a)
    assert gate.inflight_id == request_a
    assert gate.reserve_admission() is False

    assert gate.publish_and_give(request_a) is True
    gate.finish_signal(request_a)
    assert gate.inflight_id == 0
    assert gate.semaphore_token == 1

    assert gate.reserve_admission() is True
    gate.drain_stale_signal()
    request_b = gate.commit_admission()
    assert request_b == 2
    assert gate.semaphore_token == 0
    assert gate.completed_id == 0
    assert request_b != request_a
    assert gate.begin() == request_b
    assert gate.publish_and_give(request_b) is True
    gate.finish_signal(request_b)
    assert gate.consume_and_acknowledge(request_b) is True
    assert gate.inflight_id == 0


def test_probe_gate_releases_when_timeout_arrives_after_signal():
    gate = ProbeGateModel()
    assert gate.reserve_admission() is True
    gate.drain_stale_signal()
    request_a = gate.commit_admission()
    assert gate.begin() == request_a

    assert gate.publish_and_give(request_a) is True
    gate.finish_signal(request_a)
    assert gate.inflight_id == request_a
    assert gate.signaled_id == request_a

    gate.timeout(request_a)
    assert gate.inflight_id == 0
    assert gate.completed_id == 0
    assert gate.signaled_id == 0
    assert gate.semaphore_token == 1

    assert gate.reserve_admission() is True
    gate.drain_stale_signal()
    assert gate.semaphore_token == 0
    assert gate.commit_admission() == 2


def test_probe_gate_allows_acknowledgment_before_producer_post_signal_cleanup():
    gate = ProbeGateModel()
    assert gate.reserve_admission() is True
    gate.drain_stale_signal()
    request_a = gate.commit_admission()
    assert gate.begin() == request_a
    assert gate.publish_and_give(request_a) is True

    assert gate.consume_and_acknowledge(request_a) is True
    gate.finish_signal(request_a)
    assert gate.inflight_id == 0
    assert gate.completed_id == 0
    assert gate.signaled_id == 0
    assert gate.reserve_admission() is True


def test_probe_gate_cancels_only_work_that_has_not_started():
    gate = ProbeGateModel()
    assert gate.reserve_admission() is True
    gate.drain_stale_signal()
    request_a = gate.commit_admission()
    gate.timeout(request_a)
    assert gate.pending_id == 0
    assert gate.inflight_id == 0
    assert gate.begin() == 0
    assert gate.reserve_admission() is True
    gate.drain_stale_signal()
    assert gate.commit_admission() == 2


def test_firmware_binds_scroll_and_compose_probes_to_owned_gate_and_flushes_static_pages():
    source = read("main/ui/ui_phase1.c")

    assert "d1l_ui_probe_gate_t s_scroll_probe_gate" in source
    assert "d1l_ui_probe_gate_t s_compose_probe_gate" in source
    assert "gate->admission_reserved || gate->pending_id != 0U" in source
    assert "uint32_t signaled_id;" in source

    timeout_gate = function_slice(
        source,
        "static void probe_gate_timeout",
        "static bool probe_gate_publish",
    )
    assert "gate->signaled_id == request_id" in timeout_gate
    assert "gate->inflight_id = 0U;" in timeout_gate

    scroll_api = function_slice(
        source,
        "esp_err_t d1l_ui_phase1_scroll_probe",
        "esp_err_t d1l_ui_phase1_compose_probe",
    )
    compose_api = function_slice(
        source,
        "esp_err_t d1l_ui_phase1_compose_probe",
        "const char *d1l_ui_phase1_active_tab_name",
    )
    for body, gate in (
        (scroll_api, "s_scroll_probe_gate"),
        (compose_api, "s_compose_probe_gate"),
    ):
        reserve = body.index(f"probe_gate_reserve_admission(&{gate})")
        drain = body.index("xSemaphoreTake", reserve)
        commit = body.index(f"probe_gate_commit_admission(&{gate})")
        assert reserve < drain < commit
        assert f"probe_gate_timeout(&{gate}, request_id)" in body
        assert f"{gate}.completed_id != request_id" in body
        assert f"probe_gate_acknowledge(&{gate}, request_id)" in body

    scroll_finish = function_slice(
        source,
        "static void finish_pending_scroll_probe",
        "static void process_pending_scroll_probe",
    )
    compose_finish = function_slice(
        source,
        "static void finish_pending_compose_probe",
        "static void process_pending_compose_probe",
    )
    for body in (scroll_finish, compose_finish):
        publish = body.index("probe_gate_publish")
        signal = body.index("xSemaphoreGive")
        release = body.index("probe_gate_finish_signal")
        assert publish < signal < release

    finish_signal = function_slice(
        source,
        "static void probe_gate_finish_signal",
        "static bool probe_gate_acknowledge",
    )
    assert "gate->signaled_id = request_id;" in finish_signal
    assert "gate->abandoned_id == request_id" in finish_signal

    run_scroll = function_slice(
        source,
        "static void run_scroll_probe_on_ui_task",
        "static uint32_t begin_pending_scroll_probe",
    )
    assert run_scroll.index("force_ui_layout_repaint();") < run_scroll.index("if (static_page)")
    assert "D1L_UI_SCROLL_PROBE_TIMEOUT_MS = 5000U" in source
