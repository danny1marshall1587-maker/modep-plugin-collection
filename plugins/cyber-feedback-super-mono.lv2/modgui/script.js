function (event) {
    var pedal = event.icon;

    // Rotary Knob Drag Handling
    pedal.find('.custom-knob-dial').each(function () {
        var dial = $(this);
        var symbol = dial.data('symbol');
        var min = parseFloat(dial.data('min'));
        var max = parseFloat(dial.data('max'));
        var def = parseFloat(dial.data('default'));

        var rotor = dial.find('.knob-rotor');
        var curVal = def;

        function updateRotation(val) {
            var norm = (val - min) / (max - min);
            norm = Math.max(0, Math.min(1, norm));
            var deg = -140 + norm * 280;
            rotor.css('transform', 'rotate(' + deg + 'deg)');
        }

        updateRotation(curVal);

        var startY = 0;
        var startVal = curVal;

        dial.on('mousedown touchstart', function (e) {
            e.preventDefault();
            e.stopPropagation();
            startY = e.pageY || (e.originalEvent.touches && e.originalEvent.touches[0].pageY);
            startVal = curVal;

            $(document).on('mousemove.knob touchmove.knob', function (moveEvent) {
                var curY = moveEvent.pageY || (moveEvent.originalEvent.touches && moveEvent.originalEvent.touches[0].pageY);
                var dy = startY - curY;
                var range = max - min;
                var step = range / 150.0;
                var newVal = startVal + dy * step;
                newVal = Math.max(min, Math.min(max, newVal));

                curVal = newVal;
                updateRotation(curVal);

                event.set_port_value(symbol, curVal);
                pedal.find('.mod-knob-image[mod-port-symbol="' + symbol + '"]').val(curVal).trigger('change');
            });

            $(document).on('mouseup.knob touchend.knob', function () {
                $(document).off('mousemove.knob touchmove.knob');
                $(document).off('mouseup.knob touchend.knob');
            });
        });
    });

    // Room Routing Toggle Switch Handling
    var roomToggle = pedal.find('.room-toggle-switch');
    roomToggle.on('click', function (e) {
        e.preventDefault();
        e.stopPropagation();
        var cur = parseFloat(roomToggle.attr('data-val') || 0);
        var next = (cur > 0.5) ? 0.0 : 1.0;
        roomToggle.attr('data-val', next);

        if (next > 0.5) {
            roomToggle.find('.opt-out').removeClass('active');
            roomToggle.find('.opt-loop').addClass('active');
        } else {
            roomToggle.find('.opt-loop').removeClass('active');
            roomToggle.find('.opt-out').addClass('active');
        }

        event.set_port_value('room_in_loop', next);
    });

    // Hold / Stomp Trigger Button Handling
    var triggerBtn = pedal.find('.trigger-stomp-button');
    var isTriggerHeld = false;

    triggerBtn.on('mousedown touchstart', function (e) {
        e.preventDefault();
        e.stopPropagation();
        isTriggerHeld = true;
        triggerBtn.addClass('active');
        event.set_port_value('feedback', 1.0);
    });

    $(document).on('mouseup.trigger touchend.trigger', function () {
        if (isTriggerHeld) {
            isTriggerHeld = false;
            triggerBtn.removeClass('active');
            event.set_port_value('feedback', 0.0);
        }
    });

    // Handle Incoming Host Events
    if (event.type === 'change') {
        var symbol = event.symbol;
        var value = event.value;

        var dial = pedal.find('.custom-knob-dial[data-symbol="' + symbol + '"]');
        if (dial.length) {
            var min = parseFloat(dial.data('min'));
            var max = parseFloat(dial.data('max'));
            var norm = (value - min) / (max - min);
            norm = Math.max(0, Math.min(1, norm));
            var deg = -140 + norm * 280;
            dial.find('.knob-rotor').css('transform', 'rotate(' + deg + 'deg)');
        }

        if (symbol === 'room_in_loop') {
            roomToggle.attr('data-val', value);
            if (value > 0.5) {
                roomToggle.find('.opt-out').removeClass('active');
                roomToggle.find('.opt-loop').addClass('active');
            } else {
                roomToggle.find('.opt-loop').removeClass('active');
                roomToggle.find('.opt-out').addClass('active');
            }
        }
    }
}
