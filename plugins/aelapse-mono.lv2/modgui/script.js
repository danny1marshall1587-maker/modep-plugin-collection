function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var timeValEl = pedal.find('[mod-role="delay_time_val"]');
    var decayValEl = pedal.find('[mod-role="spring_decay_val"]');
    var modeTagEl = pedal.find('[mod-role="delay_mode_tag"]');
    var satTagEl = pedal.find('[mod-role="sat_tag"]');
    var widthTagEl = pedal.find('[mod-role="width_tag"]');

    var currentTime = 0.35;
    var currentDecay = 65;
    var currentMode = 0;
    var currentSat = 35;
    var currentWidth = 100;

    var modeNames = ["MODE: NORMAL TAPE", "MODE: PING-PONG", "MODE: REVERSE TAPE"];

    function updateVisuals() {
        if (timeValEl) {
            var ms = Math.round(currentTime * 1000);
            timeValEl.text(ms >= 1000 ? (currentTime.toFixed(2) + " s") : (ms + " ms"));
        }
        if (decayValEl) {
            decayValEl.text("DECAY: " + Math.round(currentDecay) + "%");
        }
        if (modeTagEl) {
            modeTagEl.text(modeNames[currentMode] || "MODE: NORMAL TAPE");
        }
        if (satTagEl) {
            satTagEl.text("SAT: " + Math.round(currentSat) + "%");
        }
        if (widthTagEl) {
            widthTagEl.text("WIDTH: " + Math.round(currentWidth) + "%");
        }
    }

    function handle_event(symbol, value) {
        if (symbol === 'delay_time') {
            currentTime = value;
            updateVisuals();
        } else if (symbol === 'springs_decay') {
            currentDecay = value;
            updateVisuals();
        } else if (symbol === 'delay_mode') {
            currentMode = Math.round(value);
            updateVisuals();
        } else if (symbol === 'delay_saturation') {
            currentSat = value;
            updateVisuals();
        } else if (symbol === 'springs_width') {
            currentWidth = value;
            updateVisuals();
        }
    }

    if (event.type === 'start') {
        var ports = event.ports;
        for (var p in ports) {
            handle_event(ports[p].symbol, ports[p].value);
        }
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
