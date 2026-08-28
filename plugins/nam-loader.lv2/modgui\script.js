function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    // 24 Professional Tone 3000 / NAM Captures in 6 organized folders
    var ampModels = [
        // --- 01 - American Clean & Vintage ---
        { id: 0,  name: "Fender '65 Deluxe Reverb", folder: "01 - American Clean & Vintage", cat: "Clean Bell", desc: "Lush sparkling American clean with bell-like top end and scooped mids" },
        { id: 1,  name: "Fender '59 Tweed Bassman", folder: "01 - American Clean & Vintage", cat: "Vintage Tweed", desc: "Warm harmonic tweed drive with dynamic touch responsiveness and punch" },
        { id: 2,  name: "Dumble ODS Clean Channel", folder: "01 - American Clean & Vintage", cat: "Boutique Clean", desc: "Ultra-rare pristine boutique clean with legendary harmonic bloom and sag" },
        { id: 3,  name: "Matchless DC-30 Clean", folder: "01 - American Clean & Vintage", cat: "Class A Clean", desc: "Boutique EL84 chime with 3D harmonic richness and glass-smooth top" },

        // --- 02 - British Chime & Crunch ---
        { id: 4,  name: "Vox AC30 Top Boost 1964", folder: "02 - British Chime & Crunch", cat: "Chime Clean", desc: "Iconic British EL84 top-end bite, chime sparkle, and singing crunch" },
        { id: 5,  name: "Marshall Bluesbreaker 1962", folder: "02 - British Chime & Crunch", cat: "Edge of Breakup", desc: "Smooth organic vintage breakup with warm singing sustain and punch" },
        { id: 6,  name: "Marshall JTM45 Plexi", folder: "02 - British Chime & Crunch", cat: "Vintage Plexi", desc: "Full-bodied KT66 tube sag with fat woody cleans pushing into roaring crunch" },
        { id: 7,  name: "Orange Rockerverb 50", folder: "02 - British Chime & Crunch", cat: "British Crunch", desc: "Thick mid-forward British crunch with velvety low-end and saturation" },

        // --- 03 - High Gain & Modern Lead ---
        { id: 8,  name: "Marshall JCM800 2203", folder: "03 - High Gain & Modern Lead", cat: "Classic 80s Rock", desc: "Tight punchy 80s British rock rhythm and aggressive roaring harmonics" },
        { id: 9,  name: "Soldano SLO-100 Super Lead", folder: "03 - High Gain & Modern Lead", cat: "High Gain Lead", desc: "Rich saturated boutique high gain with infinite singing sustain and clarity" },
        { id: 10, name: "Peavey 5150 Block Letter", folder: "03 - High Gain & Modern Lead", cat: "Modern Metal", desc: "Brutal aggressive high gain metal rhythm with razor-sharp attack" },
        { id: 11, name: "Mesa Dual Rectifier Multi-Watt", folder: "03 - High Gain & Modern Lead", cat: "Modern High Gain", desc: "Huge wall-of-sound American high gain with scooped mids and massive chunk" },
        { id: 12, name: "Friedman BE-100 Brown Eye", folder: "03 - High Gain & Modern Lead", cat: "Boutique Lead", desc: "Ultra-refined hot-rodded Plexi tone with crisp articulation" },
        { id: 13, name: "Bogner Ecstasy 101B Red", folder: "03 - High Gain & Modern Lead", cat: "Boutique Heavy", desc: "Complex woody high gain with rich harmonic overtone bloom and fat punch" },
        { id: 14, name: "ENGL Powerball II Lead", folder: "03 - High Gain & Modern Lead", cat: "Ultra Modern Metal", desc: "Precision German high gain with razor-focused mids and crushing sub punch" },
        { id: 15, name: "Diezel VH4 Channel 3", folder: "03 - High Gain & Modern Lead", cat: "Industrial Heavy", desc: "Monumental 3D high-gain punch made famous by Tool and Metallica" },

        // --- 04 - Boutique Bass & Acoustic ---
        { id: 16, name: "Ampeg SVT-CL Classic Stack", folder: "04 - Bass Rigs", cat: "Tube Bass", desc: "Massive 300W tube bass stack with thunderous low-end authority and growl" },
        { id: 17, name: "Darkglass Microtubes B7K", folder: "04 - Bass Rigs", cat: "Modern Bass Grit", desc: "Punchy modern bass preamp with aggressive clank and harmonic distortion" },
        { id: 18, name: "Acoustic Tube DI Preamp", folder: "04 - Bass Rigs", cat: "Acoustic Preamp", desc: "Warm pristine tube DI response for acoustic guitars with natural resonance" },

        // --- 05 - Overdrive & Fuzz Pedals ---
        { id: 19, name: "Klon Centaur Gold Horsie", folder: "05 - Overdrive & Boost Pedals", cat: "Transparent OD", desc: "Mythical transparent overdrive with germanium diode clipping" },
        { id: 20, name: "Ibanez TS808 Tube Screamer", folder: "05 - Overdrive & Boost Pedals", cat: "Mid Boost OD", desc: "Iconic mid-hump overdrive for tightening amp low end and pushing leads" },
        { id: 21, name: "ProCo Rat 2 Vintage LM308", folder: "05 - Overdrive & Boost Pedals", cat: "Distortion / Fuzz", desc: "Gritty versatile distortion with signature asymmetrical filter bite" },
        { id: 22, name: "Electro-Harmonix Big Muff Pi", folder: "05 - Overdrive & Boost Pedals", cat: "Fuzz Sustain", desc: "Massive scooped fuzz with infinite singing sustain and thick low-end wool" },
        { id: 23, name: "King of Tone High Gain Side", folder: "05 - Overdrive & Boost Pedals", cat: "Boutique Overdrive", desc: "Dual-stage analog overdrive with sweet dynamic touch sensitivity" }
    ];

    // Build Folder Hierarchy Tree
    var folderTree = {
        name: "Root",
        path: "/",
        folders: {},
        files: []
    };

    // Populate tree
    ampModels.forEach(function (m) {
        if (!folderTree.folders[m.folder]) {
            folderTree.folders[m.folder] = {
                name: m.folder,
                path: "/" + m.folder + "/",
                folders: {},
                files: []
            };
        }
        folderTree.folders[m.folder].files.push(m);
    });

    var currentPathStack = []; // empty = root
    var activeModelId = 8; // JCM800 default

    // UI Elements
    var nameEl = pedal.find('[mod-role="model_name"]');
    var catEl = pedal.find('[mod-role="cat_badge"]');
    var descEl = pedal.find('[mod-role="model_desc"]');
    var cabEl = pedal.find('[mod-role="cab_tag"]');
    var crumbEl = pedal.find('[mod-role="folder_crumb"]');

    var overlay = pedal.find('[mod-role="browser_overlay"]');
    var pathDisplay = pedal.find('[mod-role="current_path_display"]');
    var itemsList = pedal.find('[mod-role="browser_items_list"]');
    var btnBack = pedal.find('[mod-role="btn_back"]');
    var btnClose = pedal.find('[mod-role="btn_close"]');
    var btnOpen = pedal.find('[mod-role="open_browser_btn"]');
    var oledDisplay = pedal.find('.nam-display');
    var prevBtn = pedal.find('[mod-role="prev_model"]');
    var nextBtn = pedal.find('[mod-role="next_model"]');

    function getCurrentFolderNode() {
        var node = folderTree;
        for (var i = 0; i < currentPathStack.length; i++) {
            var fName = currentPathStack[i];
            if (node.folders[fName]) {
                node = node.folders[fName];
            }
        }
        return node;
    }

    function renderBrowser() {
        var node = getCurrentFolderNode();
        itemsList.empty();

        // Update Pinned Sticky Top Navigation Bar
        if (currentPathStack.length === 0) {
            pathDisplay.text("📁 / Root Collection /");
            btnBack.addClass("disabled");
        } else {
            pathDisplay.text("📁 / " + currentPathStack.join(" / ") + " /");
            btnBack.removeClass("disabled");
        }

        // 1. Render Subfolders (if any)
        var folderKeys = Object.keys(node.folders);
        folderKeys.sort().forEach(function (fName) {
            var subNode = node.folders[fName];
            var count = subNode.files.length;
            var folderHtml = $(
                '<div class="nam-folder-item">' +
                '  <span class="folder-icon">📁</span>' +
                '  <span class="folder-name">' + fName + '</span>' +
                '  <span class="folder-count">' + count + ' NAMs</span>' +
                '</div>'
            );

            folderHtml.on('click', function (e) {
                e.stopPropagation();
                currentPathStack.push(fName);
                renderBrowser();
            });

            itemsList.append(folderHtml);
        });

        // 2. Render Individual NAM Files (if any)
        node.files.forEach(function (m) {
            var isActive = (m.id === activeModelId);
            var fileHtml = $(
                '<div class="nam-file-item' + (isActive ? ' active' : '') + '">' +
                '  <div class="nam-file-top">' +
                '    <span class="file-icon">⚡</span>' +
                '    <span class="file-name">' + m.name + '</span>' +
                (isActive ? '    <span class="active-badge">✓ ACTIVE</span>' : '') +
                '  </div>' +
                '  <div class="nam-file-desc">' + m.desc + '</div>' +
                '</div>'
            );

            fileHtml.on('click', function (e) {
                e.stopPropagation();
                selectModel(m.id);
                renderBrowser();
            });

            itemsList.append(fileHtml);
        });
    }

    function selectModel(id) {
        var mId = Math.max(0, Math.min(ampModels.length - 1, Math.round(id)));
        activeModelId = mId;
        var info = ampModels[mId];
        if (info) {
            nameEl.text(info.name);
            catEl.text(info.cat);
            descEl.text(info.desc);
            crumbEl.text("📁 " + info.folder.split(" - ")[1] || info.folder);

            // Sync hidden port input so MOD-UI saves this exact model with the patch!
            pedal.find('input[mod-port-symbol="profile"]').val(mId).trigger('change');
            if (event.set_port_value) {
                event.set_port_value('profile', mId);
            }
        }
    }

    // --- Button Event Handlers ---
    btnBack.on('click', function (e) {
        e.stopPropagation();
        if (currentPathStack.length > 0) {
            currentPathStack.pop();
            renderBrowser();
        }
    });

    btnOpen.on('click', function (e) {
        e.stopPropagation();
        // Auto-navigate to active model's folder
        var curInfo = ampModels[activeModelId];
        if (curInfo && curInfo.folder) {
            currentPathStack = [curInfo.folder];
        } else {
            currentPathStack = [];
        }
        renderBrowser();
        overlay.fadeIn(120);
    });

    btnClose.on('click', function (e) {
        e.stopPropagation();
        overlay.fadeOut(100);
    });

    prevBtn.on('click', function (e) {
        e.stopPropagation();
        var prevId = (activeModelId - 1 + ampModels.length) % ampModels.length;
        selectModel(prevId);
    });

    nextBtn.on('click', function (e) {
        e.stopPropagation();
        var nextId = (activeModelId + 1) % ampModels.length;
        selectModel(nextId);
    });

    function handle_event(symbol, value) {
        if (symbol === 'profile') {
            var valId = Math.round(value);
            activeModelId = valId;
            var info = ampModels[valId];
            if (info) {
                nameEl.text(info.name);
                catEl.text(info.cat);
                descEl.text(info.desc);
                crumbEl.text("📁 " + (info.folder.split(" - ")[1] || info.folder));
            }
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
