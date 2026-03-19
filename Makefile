# ── Config ────────────────────────────────────────────────────────────────────
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug

# Auto-detect Nix store Qt 6 paths
NIX_QTBASE   ?= $(shell ls -d /nix/store/*-qtbase-6.* 2>/dev/null | grep -v '\.drv$$' | grep -v dev | head -1)
NIX_QTDECL   ?= $(shell ls -d /nix/store/*-qtdeclarative-6.* 2>/dev/null | grep -v '\.drv$$' | grep -v dev | head -1)
NIX_QTREMOBJ ?= $(shell ls -d /nix/store/*-qtremoteobjects-6.* 2>/dev/null | grep -v '\.drv$$' | grep -v dev | head -1)
NIX_QT_PREFIX ?= $(NIX_QTBASE);$(NIX_QTDECL);$(NIX_QTREMOBJ)

# Auto-detect Nix store Logos SDK paths
LOGOS_HEADERS_NIX     ?= $(shell ls -d /nix/store/*logos-liblogos-headers-* 2>/dev/null | grep -v '\.drv$$' | head -1)
LOGOS_LIB_NIX         ?= $(shell ls -d /nix/store/*logos-liblogos-lib-* 2>/dev/null | grep -v '\.drv$$' | head -1)
LOGOS_SDK_HEADERS_NIX ?= $(shell ls -d /nix/store/*logos-cpp-sdk-headers-* 2>/dev/null | grep -v '\.drv$$' | head -1)
LOGOS_SDK_LIB_NIX     ?= $(shell ls -d /nix/store/*logos-cpp-sdk-lib-* 2>/dev/null | grep -v '\.drv$$' | head -1)

MODULES_DIR  ?= $(HOME)/.local/share/Logos/LogosAppNix/modules
BUILD_MOD_DIR  ?= build-module
BUILD_TEST_DIR ?= build-tests

# ── Phony targets ─────────────────────────────────────────────────────────────
.PHONY: all build-module install-module build-tests test \
        setup-nix-merged clean

# ── Nix SDK merge ─────────────────────────────────────────────────────────────
setup-nix-merged:
	@echo "-> Merging Nix SDK paths..."
	rm -rf /tmp/logos-cpp-sdk-merged /tmp/logos-liblogos-merged
	mkdir -p /tmp/logos-cpp-sdk-merged/{include,lib} /tmp/logos-liblogos-merged/{include,lib}
	@[ -n "$(LOGOS_SDK_HEADERS_NIX)" ] || (echo "ERROR: logos-cpp-sdk-headers not found in /nix/store"; exit 1)
	ln -sf $(LOGOS_SDK_HEADERS_NIX)/include/* /tmp/logos-cpp-sdk-merged/include/
	ln -sf $(LOGOS_SDK_LIB_NIX)/lib/*         /tmp/logos-cpp-sdk-merged/lib/
	ln -sf $(LOGOS_HEADERS_NIX)/include/*     /tmp/logos-liblogos-merged/include/
	ln -sf $(LOGOS_LIB_NIX)/lib/*             /tmp/logos-liblogos-merged/lib/

# ── Headless module ───────────────────────────────────────────────────────────
build-module: setup-nix-merged
	mkdir -p $(BUILD_MOD_DIR)
	cd $(BUILD_MOD_DIR) && cmake .. $(CMAKE_FLAGS) \
		-DBUILD_MODULE=ON \
		-DLOGOS_CPP_SDK_ROOT=/tmp/logos-cpp-sdk-merged \
		-DLOGOS_LIBLOGOS_ROOT=/tmp/logos-liblogos-merged \
		$(if $(NIX_QTBASE), \
		  -DCMAKE_PREFIX_PATH="$(NIX_QT_PREFIX)" \
		  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$(NIX_QTDECL)$$(echo ';')$(NIX_QTREMOBJ)") \
		&& cmake --build . --target sync_module_plugin -j$$(nproc)

install-module: build-module
	mkdir -p $(MODULES_DIR)/sync_module
	cp $(BUILD_MOD_DIR)/sync_module_plugin.so $(MODULES_DIR)/sync_module/
	cp modules/sync/manifest.json $(MODULES_DIR)/sync_module/manifest.json
	@echo "sync_module installed to $(MODULES_DIR)/sync_module"

# ── Unit tests (no Logos SDK, no display required) ────────────────────────────
build-tests:
	mkdir -p $(BUILD_TEST_DIR)
	cd $(BUILD_TEST_DIR) && cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
		$(if $(NIX_QTBASE), \
		  -DCMAKE_PREFIX_PATH="$(NIX_QT_PREFIX)" \
		  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$(NIX_QTDECL)$$(echo ';')$(NIX_QTREMOBJ)") \
		&& cmake --build . -j$$(nproc)

test: build-tests
	cd $(BUILD_TEST_DIR) && ctest --output-on-failure

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_MOD_DIR) $(BUILD_TEST_DIR) \
	       /tmp/logos-cpp-sdk-merged /tmp/logos-liblogos-merged
