"""rqt plugin wrapper for the boat-state panel."""

from rqt_gui_py.plugin import Plugin

from .boat_state_widget import BoatStateWidget
from .config_dialog import ConfigDialog
from .config_model import BoatStateConfig


class BoatStatePlugin(Plugin):
    """rqt plugin that wraps the BoatStateWidget."""

    def __init__(self, context):
        super().__init__(context)
        self.setObjectName('BoatStatePlugin')
        self._node = context.node
        self._widget = BoatStateWidget(self._node)
        self._widget.setWindowTitle('Boat State')
        if context.serial_number() > 1:
            self._widget.setWindowTitle(
                f'{self._widget.windowTitle()} ({context.serial_number()})')
        context.add_widget(self._widget)

    def shutdown_plugin(self):
        self._widget.shutdown()

    def save_settings(self, plugin_settings, instance_settings):
        config = self._widget.get_config()
        instance_settings.set_value('config_yaml', config.to_yaml())

    def restore_settings(self, plugin_settings, instance_settings):
        yaml_str = instance_settings.value('config_yaml', '')
        if yaml_str:
            try:
                config = BoatStateConfig.from_yaml(yaml_str)
                self._widget.load_config(config)
            except Exception as exc:
                self._node.get_logger().warn(
                    f'Failed to restore settings: {exc}')

    def trigger_configuration(self):
        """Called when the user clicks the wrench icon in rqt."""
        dialog = ConfigDialog(self._widget.get_config(), self._widget)
        if dialog.exec_():
            self._widget.load_config(dialog.get_config())
