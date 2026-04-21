from setuptools import find_packages, setup

package_name = 'rqt_operator_log'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml', 'plugin.xml']),
    ],
    install_requires=['setuptools'],
    tests_require=['pytest'],
    zip_safe=True,
    maintainer='Roland Arsenault',
    maintainer_email='roland@ccom.unh.edu',
    description='Operator logbook rqt plugin with rosbag2 recording and recovery.',
    license='BSD-3-Clause',
    entry_points={
        'console_scripts': [
            'operator_log = rqt_operator_log.operator_log_standalone:main',
        ],
    },
)
