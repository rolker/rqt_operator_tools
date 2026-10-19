from setuptools import find_packages, setup

package_name = 'rqt_ping_profile'

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
    description="A boat's current multibeam ping seen from the front: swath "
                'lines and the bathymetry profile under the boat.',
    license='BSD-3-Clause',
    entry_points={
        'console_scripts': [
            'ping_profile = rqt_ping_profile.ping_profile_standalone:main',
        ],
    },
)
