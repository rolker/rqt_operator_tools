"""rqt plugin wrapper for the survey-coverage scoreboard."""

from rqt_gui_py.plugin import Plugin

from .config_dialog import ConfigDialog
from .coverage_widget import CoverageWidget
from .score_model import CoverageConfig


class CoveragePlugin(Plugin):
    """rqt plugin that wraps the CoverageWidget."""

    def __init__(self, context):
        """Create the widget on the plugin's node and add it to the rqt window."""
        super().__init__(context)
        self.setObjectName('CoveragePlugin')
        self._node = context.node
        self._widget = CoverageWidget(self._node)
        self._widget.setWindowTitle('Survey Coverage')
        if context.serial_number() > 1:
            self._widget.setWindowTitle(
                f'{self._widget.windowTitle()} ({context.serial_number()})')
        context.add_widget(self._widget)

    def shutdown_plugin(self):
        """Stop the refresh timer and destroy the subscription."""
        self._widget.shutdown()

    def save_settings(self, plugin_settings, instance_settings):
        """Persist the configuration (including the current view) as YAML."""
        instance_settings.set_value('config_yaml', self._widget.get_config().to_yaml())

    def restore_settings(self, plugin_settings, instance_settings):
        """Restore the configuration; keep the defaults when it cannot be read."""
        yaml_str = instance_settings.value('config_yaml', '')
        if yaml_str:
            try:
                self._widget.load_config(CoverageConfig.from_yaml(yaml_str))
            except Exception as exc:
                self._node.get_logger().warning(
                    f'Failed to restore settings: {exc}')

    def trigger_configuration(self):
        """Open the settings dialog (the wrench icon in rqt)."""
        dialog = ConfigDialog(self._widget.get_config(),
                              self._widget.seen_area_ids(), self._widget)
        if dialog.exec_():
            self._widget.load_config(dialog.get_config())
