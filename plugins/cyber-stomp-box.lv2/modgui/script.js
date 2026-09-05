function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var slotNames = [
        "01_Acoustic_Cajon.wav",
        "02_Deep_Wood_Stomp.wav",
        "03_Punchy_Studio_Kick.wav",
        "04_Vintage_Boom_Kick.wav",
        "05_Foot_Tambourine.wav",
        "06_Foot_Snare.wav",
        "Custom User File"
    ];

    var slotDescs = [
        "Acoustic Cajon Slap & Chamber",
        "Hardwood Stage Floor Plate",
        "Punchy 22\" Studio Bass Drum",
        "Deep Resonant Acoustic Kick",
        "Acoustic Tambourine Pulse",
        "Acoustic Rimshot & Wire Sizzle",
        "Loaded from Audio Samples Folder"
    ];

    var displayDesc = pedal.find('[mod-role="display_desc"]');
    var sampleNameText = pedal.find('.sample-name-text');
    var hitStatus = pedal.find('[mod-role="hit_status"]');
    var hitLed = pedal.find('[mod-role="hit_led"]');
    var velBadge = pedal.find('[mod-role="vel_badge"]');
    var routeBadge = pedal.find('[mod-role="route_badge"]');

    var currentSlot = 0;
    var currentVelMode = 1;
    var currentRoute = 0;

    function refreshDisplay() {
        var sIdx = Math.max(0, Math.min(Math.round(currentSlot), slotNames.length - 1));
        if (displayDesc && displayDesc.length) {
            displayDesc.text(slotDescs[sIdx]);
        }
        if (sampleNameText && sampleNameText.length && sIdx < 6) {
            if (!sampleNameText.hasClass('custom-set')) {
                sampleNameText.text(slotNames[sIdx]);
            }
        }

        if (velBadge && velBadge.length) {
            velBadge.text(currentVelMode >= 0.5 ? "FIXED VEL 100%" : "DYNAMIC VEL");
        }
        if (routeBadge && routeBadge.length) {
            routeBadge.text(currentRoute >= 0.5 ? "SPLIT (L=GTR, R=PA)" : "MIX OUT L/R");
        }
    }

    var hitTimer = null;
    function flashHitLed(isActive) {
        if (isActive) {
            if (hitLed) hitLed.addClass('hit');
            if (hitStatus) hitStatus.text("STOMP!").addClass('flashing');
        } else {
            if (hitLed) hitLed.removeClass('hit');
            if (hitStatus) hitStatus.text("READY").removeClass('flashing');
        }
    }

    function triggerVisualHit() {
        flashHitLed(true);
        if (hitTimer) clearTimeout(hitTimer);
        hitTimer = setTimeout(function () {
            flashHitLed(false);
        }, 120);
    }

    function handle_event(symbol, value) {
        if (symbol === 'sample_slot') {
            currentSlot = value;
            refreshDisplay();
        } else if (symbol === 'fixed_vel') {
            currentVelMode = value;
            refreshDisplay();
        } else if (symbol === 'output_mode') {
            currentRoute = value;
            refreshDisplay();
        } else if (symbol === 'led_activity') {
            if (value > 0.05) {
                flashHitLed(true);
            } else {
                flashHitLed(false);
            }
        } else if (symbol === 'trigger') {
            triggerVisualHit();
        }
    }

    if (event.type === 'start') {
        var ports = event.ports;
        for (var p in ports) {
            handle_event(ports[p].symbol, ports[p].value);
        }

        // Add immediate visual feedback when user clicks/taps stomp plate
        var stompPlate = pedal.find('.stomp-plate');
        stompPlate.off('mousedown.stomp touchstart.stomp').on('mousedown.stomp touchstart.stomp', function (e) {
            // Keep right-click clean for MOD-UI MIDI Learn
            if (e.which === 3) return;
            triggerVisualHit();
        });
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
