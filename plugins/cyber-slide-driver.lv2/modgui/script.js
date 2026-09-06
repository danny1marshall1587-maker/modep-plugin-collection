function (event, utils) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX = 140;
    var ROT_RANGE = ROT_MAX - ROT_MIN;

    function updateKnobDisplay(dial, val) {
        if (!dial || !dial.length) return;
        var minVal = parseFloat(dial.attr('data-min'));
        var maxVal = parseFloat(dial.attr('data-max'));
        var clamped = Math.max(minVal, Math.min(maxVal, val));
        var norm = (clamped - minVal) / (maxVal - minVal);
        var deg = ROT_MIN + (norm * ROT_RANGE);
        dial.find('.knob-rotor').css('transform', 'translate(-50%, -100%) rotate(' + deg + 'deg)');
        dial.data('current-val', clamped);
        return clamped;
    }

    function updateSwitchDisplay(sw, val) {
        if (!sw || !sw.length) return;
        var isOn = parseFloat(val) > 0.5;
        if (isOn) {
            sw.addClass('on').removeClass('off');
            sw.find('.switch-lever').css('transform', 'translate(-50%, -50%) rotate(25deg)');
            sw.find('.switch-led').addClass('lit');
        } else {
            sw.addClass('off').removeClass('on');
            sw.find('.switch-lever').css('transform', 'translate(-50%, -50%) rotate(-25deg)');
            sw.find('.switch-led').removeClass('lit');
        }
        sw.data('current-val', isOn ? 1.0 : 0.0);
    }

    function sendPortValue(sym, val) {
        if (utils && typeof utils.set_port_value === 'function') {
            utils.set_port_value(sym, val);
        } else if (typeof event.set_port_value === 'function') {
            event.set_port_value(sym, val);
        }
        pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val(val).trigger('change');
        if (event.settings && event.settings.length) {
            event.settings.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val(val).trigger('change');
        }
    }

    function bindControls(container) {
        if (!container || !container.length) return;

        // Rotary Dials
        container.find('.custom-knob-dial').each(function () {
            var dial = $(this);
            var sym = dial.attr('data-symbol');
            if (!sym) return;

            var minVal = parseFloat(dial.attr('data-min'));
            var maxVal = parseFloat(dial.attr('data-max'));
            var defVal = parseFloat(dial.attr('data-default'));
            var curVal = dial.data('current-val');
            if (typeof curVal === 'undefined') {
                curVal = defVal;
                updateKnobDisplay(dial, curVal);
            }

            dial.off('mousedown.pedal_drag touchstart.pedal_drag').on('mousedown.pedal_drag touchstart.pedal_drag', function (e) {
                e.preventDefault();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = defVal;

                $(document).on('mousemove.pedal_drag touchmove.pedal_drag', function (me) {
                    var curY = (me.touches && me.touches.length) ? me.touches[0].clientY : me.clientY;
                    var dy = startY - curY;
                    var valRange = maxVal - minVal;
                    var newVal = startVal + (dy / 150.0) * valRange;
                    var clamped = updateKnobDisplay(dial, newVal);
                    sendPortValue(sym, clamped);
                });

                $(document).on('mouseup.pedal_drag touchend.pedal_drag', function () {
                    $(document).off('.pedal_drag');
                });
            });
        });

        // Ratio Toggle Switch
        container.find('.custom-toggle-switch').each(function () {
            var sw = $(this);
            var sym = sw.attr('data-symbol');
            if (!sym) return;

            var defVal = parseFloat(sw.attr('data-default')) || 0.0;
            var curVal = sw.data('current-val');
            if (typeof curVal === 'undefined') {
                curVal = defVal;
                updateSwitchDisplay(sw, curVal);
            }

            sw.off('click.pedal_sw').on('click.pedal_sw', function (e) {
                e.preventDefault();
                var current = sw.data('current-val') || 0.0;
                var next = (current > 0.5) ? 0.0 : 1.0;
                updateSwitchDisplay(sw, next);
                sendPortValue(sym, next);
            });
        });

        // Master Zero Noise Pill Button
        container.find('#sd-gate-btn').off('click.pedal_gate').on('click.pedal_gate', function (e) {
            e.preventDefault();
            var isAct = $(this).hasClass('active');
            var next = isAct ? 0.0 : 1.0;
            $(this).toggleClass('active', next > 0.5);
            sendPortValue('zero_noise', next);
        });

        // Clean Comp Mode Pill Button
        container.find('#sd-clean-btn').off('click.pedal_clean').on('click.pedal_clean', function (e) {
            e.preventDefault();
            var isAct = $(this).hasClass('active');
            var next = isAct ? 0.0 : 1.0;
            $(this).toggleClass('active', next > 0.5);
            sendPortValue('clean_mode', next);
        });
    }

    // Bidirectional sync handler
    function handleEvent(symbol, value) {
        if (!pedal || !pedal.length) return;
        var fVal = parseFloat(value);
        if (isNaN(fVal)) return;

        var dial = pedal.find('.custom-knob-dial[data-symbol="' + symbol + '"]');
        if (dial.length) {
            updateKnobDisplay(dial, fVal);
            return;
        }

        var sw = pedal.find('.custom-toggle-switch[data-symbol="' + symbol + '"]');
        if (sw.length) {
            updateSwitchDisplay(sw, fVal);
            return;
        }

        if (symbol === 'zero_noise') {
            pedal.find('#sd-gate-btn').toggleClass('active', fVal > 0.5);
        } else if (symbol === 'clean_mode') {
            pedal.find('#sd-clean-btn').toggleClass('active', fVal > 0.5);
        }
    }

    if (event.type === 'change' && event.symbol) {
        handleEvent(event.symbol, event.value);
        return;
    }

    if (event.type === 'start') {
        bindControls(pedal);
        if (event.settings && event.settings.length) {
            bindControls(event.settings);
        }
        if (event.ports) {
            for (var i = 0; i < event.ports.length; i++) {
                var p = event.ports[i];
                handleEvent(p.symbol, p.value);
            }
        }
    }

    event.handle_event = function (symbol, value) {
        handleEvent(symbol, value);
    };
}
