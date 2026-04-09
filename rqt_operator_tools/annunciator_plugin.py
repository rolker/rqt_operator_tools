"""rqt plugin wrapper for the annunciator panel."""

from python_qt_binding.QtWidgets import QFileDialog
from rqt_gui_py.plugin import Plugin

from .annunciator_widget import AnnunciatorWidget
from .config_dialog import ConfigDialog
from .config_model import AnnunciatorConfig, IndicatorConfig


class AnnunciatorPlugin(Plugin):
    """rqt plugin that wraps the AnnunciatorWidget."""

    def __init__(self, context):
        super().__init__(context)
        self.setObjectName('AnnunciatorPlugin')
        self._node = context.node
        self._widget = AnnunciatorWidget(self._node)
        context.add_widget(self._widget)

        # Load a default config so the widget isn't empty.
        self._load_default_config()

    def _load_default_config(self):
        """Load the default config shipped with the package."""
        from ament_index_python.packages import get_package_share_directory
        import os
        share_dir = get_package_share_directory('rqt_operator_tools')
        default_path = os.path.join(share_dir, 'config', 'default_annunciator.yaml')
        if os.path.exists(default_path):
            try:
                config = AnnunciatorConfig.from_file(default_path)
                self._widget.load_config(config)
            except Exception as exc:
                self._node.get_logger().warn(
                    f'Failed to load default config: {exc}'
                )

    def shutdown_plugin(self):
        self._widget.shutdown()

    def save_settings(self, plugin_settings, instance_settings):
        config = self._widget.get_config()
        instance_settings.set_value('config_yaml', config.to_yaml())

    def restore_settings(self, plugin_settings, instance_settings):
        yaml_str = instance_settings.value('config_yaml', '')
        if yaml_str:
            try:
                config = AnnunciatorConfig.from_yaml(yaml_str)
                self._widget.load_config(config)
            except Exception as exc:
                self._node.get_logger().warn(
                    f'Failed to restore settings: {exc}'
                )

    def trigger_configuration(self):
        """Called when the user clicks the wrench icon in rqt."""
        dialog = ConfigDialog(self._widget.get_config(), self._widget)
        if dialog.exec_():
            self._widget.load_config(dialog.get_config())
