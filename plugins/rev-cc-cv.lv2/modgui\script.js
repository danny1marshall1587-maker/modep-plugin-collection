function (event) {
    var pedal = event.icon;
    var state = {
        direction: 0,
        level: 0,
        min_v: 0,
        max_v: 10,
        curve: 0,
        smoothing: 10,
        offset: 0
    };

    function updateMeters() {
        var norm = state.level / 100.0;
        var min = state.min_v;
        var max = state.max_v;
        var off = state.offset;

        var shaped = norm;
        if (state.curve === 1) shaped = Math.pow(norm, 2);
        else if (state.curve === 2) shaped = Math.sqrt(norm);
        else if (state.curve === 3) shaped = norm * norm * (3 - 2 * norm);

        var revNorm = 1.0 - shaped;
        var activeNorm = state.direction ? revNorm : shaped;

        var mainV = (min + activeNorm * (max - min)) + off;
        var revV = (min + revNorm * (max - min)) + off;

        var mainPct = Math.max(0, Math.min(100, activeNorm * 100));
        var revPct = Math.max(0, Math.min(100, revNorm * 100));

        pedal.find('#meter-main').css('width', mainPct + '%');
        pedal.find('#meter-rev').css('width', revPct + '%');

        pedal.find('#val-main').text((mainV >= 0 ? '+' : '') + mainV.toFixed(2) + ' V');
        pedal.find('#val-rev').text((revV >= 0 ? '+' : '') + revV.toFixed(2) + ' V');

        if (state.direction) {
            pedal.find('#rev-mode-text').text('REVERSE [10V → 0V]').css('color', '#f59e0b');
            pedal.find('#rev-toggle-btn').addClass('active');
            pedal.find('#rev-btn-label').text('REVERSE DIRECTION: ON');
        } else {
            pedal.find('#rev-mode-text').text('FORWARD [0V → 10V]').css('color', '#00e5ff');
            pedal.find('#rev-toggle-btn').removeClass('active');
            pedal.find('#rev-btn-label').text('REVERSE DIRECTION: OFF');
        }
    }

    // Toggle button handler
    pedal.find('#rev-toggle-btn').on('click', function (e) {
        e.preventDefault();
        state.direction = state.direction ? 0 : 1;
        event.set_port_value('direction', state.direction);
        updateMeters();
    });

    // Knob drag interaction
    pedal.find('.custom-knob-dial').each(function () {
        var dial = $(this);
        var symbol = dial.data('symbol');
        var min = parseFloat(dial.data('min'));
        var max = parseFloat(dial.data('max'));
        var def = parseFloat(dial.data('default'));

        dial.data('val', def);

        function updateRotation(v) {
            var norm = (v - min) / (max - min);
            var deg = -135 + norm * 270;
            dial.find('.knob-rotor').css('transform', 'translateX(-50%) rotate(' + deg + 'deg)');
        }

        updateRotation(def);

        dial.on('mousedown touchstart', function (e) {
            e.preventDefault();
            var startY = e.pageY || e.originalEvent.touches[0].pageY;
            var startVal = dial.data('val');

            function onMove(me) {
                var currentY = me.pageY || (me.originalEvent.touches ? me.originalEvent.touches[0].pageY : currentY);
                var dy = startY - currentY;
                var range = max - min;
                var newVal = Math.min(max, Math.max(min, startVal + (dy / 100.0) * range));

                dial.data('val', newVal);
                updateRotation(newVal);

                if (symbol === 'level') state.level = newVal;
                else if (symbol === 'min_volts') state.min_v = newVal;
                else if (symbol === 'max_volts') state.max_v = newVal;
                else if (symbol === 'curve') state.curve = Math.round(newVal);
                else if (symbol === 'offset') state.offset = newVal;

                event.set_port_value(symbol, newVal);
                updateMeters();
            }

            function onUp() {
                $(document).off('mousemove touchmove', onMove);
                $(document).off('mouseup touchend', onUp);
            }

            $(document).on('mousemove touchmove', onMove);
            $(document).on('mouseup touchend', onUp);
        });
    });

    function handle_event(symbol, value) {
        if (symbol === 'direction') {
            state.direction = value >= 0.5 ? 1 : 0;
        } else if (symbol === 'level') {
            state.level = value;
            var dial = pedal.find('.custom-knob-dial[data-symbol="level"]');
            var min = parseFloat(dial.data('min')), max = parseFloat(dial.data('max'));
            var norm = (value - min) / (max - min);
            dial.find('.knob-rotor').css('transform', 'translateX(-50%) rotate(' + (-135 + norm * 270) + 'deg)');
        } else if (symbol === 'min_volts') {
            state.min_v = value;
        } else if (symbol === 'max_volts') {
            state.max_v = value;
        } else if (symbol === 'curve') {
            state.curve = Math.round(value);
        } else if (symbol === 'offset') {
            state.offset = value;
        }
        updateMeters();
    }

    if (event.type === 'start') {
        updateMeters();
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
