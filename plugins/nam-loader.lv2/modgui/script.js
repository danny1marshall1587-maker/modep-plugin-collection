function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ampModels = [
        { name: "Fender '65 Deluxe", cat: "Clean", desc: "Lush sparkling American clean with bell-like top end" },
        { name: "Vox AC30 Top Boost", cat: "Chime Clean", desc: "Chimey British EL84 top-end bite and harmonic sparkle" },
        { name: "Marshall Bluesbreaker", cat: "Edge of Breakup", desc: "Smooth organic vintage breakup with warm singing sustain" },
        { name: "Dumble ODS Crunch", cat: "Boutique Crunch", desc: "Legendary thick, articulate boutique lead tone with lush sag" },
        { name: "Marshall JCM800", cat: "Classic Rock", desc: "Tight punchy 80s British rock rhythm and roaring harmonics" },
        { name: "Soldano SLO-100", cat: "High Gain Lead", desc: "Rich saturated boutique high gain with infinite singing sustain" },
        { name: "Peavey 5150 Block", cat: "Modern Metal", desc: "Brutal aggressive high gain metal rhythm with tight low end" },
        { name: "Mesa Dual Rectifier", cat: "High Gain", desc: "Huge wall-of-sound modern high gain with scooped heavy chunk" },
        { name: "Friedman BE-100", cat: "Boutique Lead", desc: "Ultra-refined hot-rodded Plexi tone with crisp articulation" },
        { name: "Ampeg SVT-CL", cat: "Tube Bass", desc: "Massive tube bass stack with thunderous low-end and grit" },
        { name: "Klon Centaur", cat: "Overdrive Pedal", desc: "Mythical transparent overdrive with clean blend and warm boost" },
        { name: "TS808 Tube Screamer", cat: "Mid Boost Pedal", desc: "Iconic mid-hump drive for pushing tube amps into tight leads" }
    ];

    var nameEl = pedal.find('[mod-role="model_name"]');
    var catEl = pedal.find('[mod-role="cat_badge"]');
    var descEl = pedal.find('[mod-role="model_desc"]');
    var cabEl = pedal.find('[mod-role="cab_tag"]');

    function updateProfile(idx) {
        var p = Math.max(0, Math.min(ampModels.length - 1, Math.round(idx)));
        var info = ampModels[p];
        if (info) {
            nameEl.text(info.name);
            catEl.text(info.cat);
            descEl.text(info.desc);
        }
    }

    function handle_event(symbol, value) {
        if (symbol === 'profile') {
            updateProfile(value);
        } else if (symbol === 'cab_enable') {
            cabEl.text(value >= 0.5 ? "4x12 CAB IR: ON" : "CAB IR: BYPASS");
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
