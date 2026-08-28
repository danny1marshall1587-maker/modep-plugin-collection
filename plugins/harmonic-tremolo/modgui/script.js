/* Harmonic Tremolo Interactive Script for MODEP */
(function() {
    function initHarmonicTremolo(element) {
        if (!element || element._tremInitialized) return;
        element._tremInitialized = true;

        const footswitch = element.querySelector('.mod-footswitch');
        const light = element.querySelector('.mod-light');

        if (footswitch) {
            footswitch.addEventListener('click', function() {
                setTimeout(function() {
                    const isBypassed = element.classList.contains('mod-pedal-bypassed') || !element.classList.contains('mod-pedal-on');
                    if (light) {
                        if (isBypassed) {
                            light.classList.remove('on');
                        } else {
                            light.classList.add('on');
                        }
                    }
                }, 50);
            });
        }
    }

    if (!window._harmonicTremInit) {
        window._harmonicTremInit = true;
        document.addEventListener('DOMContentLoaded', function() {
            document.querySelectorAll('.harmonic-tremolo-pedal').forEach(initHarmonicTremolo);
        });
        const observer = new MutationObserver(function() {
            document.querySelectorAll('.harmonic-tremolo-pedal').forEach(initHarmonicTremolo);
        });
        observer.observe(document.body, { childList: true, subtree: true });
    }
})();
