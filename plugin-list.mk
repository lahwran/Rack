plugin:
	@mkdir -p plugins
	@[ -e plugins/$(DIR) ] && ( echo --- ; echo "Plugin folder plugins/$(DIR) already exists and will be overwritten. Press \033[1my\033[0m to proceed or any other key to skip this plugin." ; echo --- ; read -n1 R; [ "$$R" = "y" ] && rm -rf plugins/$(DIR) || false ) || true
ifeq (,$(findstring http,$(URL)))
	git clone http://github.com/$(URL) plugins/$(DIR) --recursive
else
	git clone $(URL) plugins/$(DIR) --recursive
endif
	make -C plugins/$(DIR)
	@echo ---
	@echo Plugin $(DIR) built.
	@echo ---

list-plugins:
	@awk -F ' *\\| *' '{ if(match($$1,"^#")) next; print "\033[1m"$$1"\033[0m" "\n\t" $$3; if(index($$2,"http")) print "\t"$$2 ; else print "\thttp://github.com/"$$2 ; print ""; }' plugin-list.txt 
	@echo ---
	@echo "Type \"make +\033[1mPluginSlug\033[0m +\033[1mPluginSlug\033[0m ...\" to install plugins (note the plus signs)."
	@echo ---

rebuild-plugins:
	for f in plugins/*; do $(MAKE) -C "$$f"; done

#ALL_PLUGINS = $(shell awk -F ' *\\| *' '{ if(match($$1,"^\#")) next; printf "+%s ", $$1 }' plugin-list.txt)

+all:
	awk -F ' *\\| *' '{ if(match($$1,"^#")) next; print "+" $$1 }' plugin-list.txt | xargs -o make -i

# Special case as it actually builds two plugins
+Fundamental:
	URL=VCVRack/Fundamental DIR=Fundamental $(MAKE) plugin
	URL=mi-rack/zFundamental DIR=zFundamental $(MAKE) plugin

PLUGIN = $(shell cat plugin-list.txt | grep -i "^$* *|")
PLUGIN_DIR = $(strip $(shell echo "$(PLUGIN)" | cut -d "|" -f 1 ))
PLUGIN_URL = $(strip $(shell echo "$(PLUGIN)" | cut -d "|" -f 2 ))

+%:
	@if [ ! -n "$(PLUGIN_DIR)" ] ; then echo --- ; echo "No such plugin: $*" ; echo "Type \"make list-plugins\" for a list of plugins known to this build script." ; echo --- ; false ; else true ; fi
	URL="$(PLUGIN_URL)" DIR="$(PLUGIN_DIR)" $(MAKE) plugin