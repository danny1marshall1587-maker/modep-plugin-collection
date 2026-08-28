function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    function initButtons() {
        var buttons = pedal.find('.dimension-pushbutton');
        if (!buttons.length || buttons.data('hasDimensionListener')) return;
        buttons.data('hasDimensionListener', true);

        buttons.on('click', function(e) {
            e.stopPropagation();
            var btn = $(this);
            var modeVal = parseInt(btn.attr('data-mode'), 10);

            buttons.removeClass('active');
            btn.addClass('active');

            pedal.find('input[mod-port-symbol="mode"]').val(modeVal).trigger('change');
            if (event.set_port_value) {
                event.set_port_value('mode', modeVal);
            }
        });
    }

    function handle_event(symbol, value) {
        if (symbol === 'mode') {
            var modeVal = Math.round(value);
            var buttons = pedal.find('.dimension-pushbutton');
            buttons.removeClass('active');
            buttons.filter('[data-mode="' + modeVal + '"]').addClass('active');
        }
    }

    initButtons();

    if (event.type === 'start') {
        var ports = event.ports;
        for (var p in ports) {
            handle_event(ports[p].symbol, ports[p].value);
        }
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
