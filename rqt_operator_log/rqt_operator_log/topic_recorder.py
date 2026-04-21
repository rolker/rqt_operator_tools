"""Subscribe to configured topics and record them to the operator log bag."""

import time

import rclpy.serialization
from rclpy.node import Node
from rclpy.qos import QoSProfile, DurabilityPolicy, ReliabilityPolicy
from rosidl_runtime_py.utilities import get_message

from .bag_manager import BagManager


class TopicRecorder:
    """Discover and subscribe to topics, writing messages to a BagManager."""

    # QoS that accepts both reliable and best-effort publishers
    _RECORDING_QOS = QoSProfile(
        depth=100,
        reliability=ReliabilityPolicy.BEST_EFFORT,
        durability=DurabilityPolicy.VOLATILE,
    )

    def __init__(self, node: Node, bag_manager: BagManager, topic_names: list[str]):
        self._node = node
        self._bag_manager = bag_manager
        self._subscriptions: dict[str, object] = {}
        self._pending = set(topic_names)

        if self._pending:
            self._discovery_timer = node.create_timer(2.0, self._discover_topics)
            # Run discovery immediately
            self._discover_topics()

    def _discover_topics(self):
        """Check for pending topics and subscribe when their types are available."""
        if not self._pending:
            self._discovery_timer.cancel()
            return

        topic_types = dict(self._node.get_topic_names_and_types())
        newly_found = []

        for topic_name in list(self._pending):
            if topic_name in topic_types and topic_types[topic_name]:
                msg_type_str = topic_types[topic_name][0]
                try:
                    self._subscribe(topic_name, msg_type_str)
                    newly_found.append(topic_name)
                except Exception as exc:
                    self._node.get_logger().warn(
                        f'Failed to subscribe to {topic_name} ({msg_type_str}): {exc}'
                    )

        for name in newly_found:
            self._pending.discard(name)

        if not self._pending:
            self._discovery_timer.cancel()

    def _subscribe(self, topic_name: str, msg_type_str: str):
        """Create a subscription and register the topic in the bag."""
        msg_class = get_message(msg_type_str)
        self._bag_manager.register_topic(topic_name, msg_type_str)

        def callback(msg):
            serialized = rclpy.serialization.serialize_message(msg)
            self._bag_manager.write_serialized(
                topic_name, serialized, time.time_ns()
            )

        sub = self._node.create_subscription(
            msg_class, topic_name, callback, self._RECORDING_QOS
        )
        self._subscriptions[topic_name] = sub
        self._node.get_logger().info(
            f'Recording topic: {topic_name} ({msg_type_str})'
        )

    def shutdown(self):
        """Destroy subscriptions and stop discovery."""
        if hasattr(self, '_discovery_timer'):
            self._discovery_timer.cancel()
        for sub in self._subscriptions.values():
            self._node.destroy_subscription(sub)
        self._subscriptions.clear()
