PLUGINS = harmonic-tremolo cyber-denoiser galaxy-strobe-tune
INSTALL_DIR ?= /var/modep/lv2

all:
	@for p in $(PLUGINS); do \
		echo "Building $$p..."; \
		$(MAKE) -C $$p all || exit 1; \
	done

bundles: all
	@mkdir -p dist
	@for p in $(PLUGINS); do \
		echo "Copying $$p.lv2 to dist/"; \
		cp -rf $$p/$$p.lv2 dist/; \
	done

install: bundles
	@echo "Installing bundles to $(INSTALL_DIR)..."
	@mkdir -p $(INSTALL_DIR)
	@cp -rf dist/*.lv2 $(INSTALL_DIR)/
	@echo "Installed plugins successfully!"
	@echo "Reloading MODEP host..."
	@systemctl restart modep-mod-host modep-mod-ui 2>/dev/null || true

clean:
	@for p in $(PLUGINS); do \
		$(MAKE) -C $$p clean; \
	done
	rm -rf dist

.PHONY: all bundles install clean
