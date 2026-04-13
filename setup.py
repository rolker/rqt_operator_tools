from setuptools import find_packages, setup

package_name = 'rqt_operator_tools'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml', 'plugin.xml']),
        ('share/' + package_name + '/config', ['config/default_annunciator.yaml']),
    ],
    install_requires=['setuptools'],
    tests_require=['pytest'],
    zip_safe=True,
    maintainer='Roland Arsenault',
    maintainer_email='roland@ccom.unh.edu',
    description='Operator station rqt plugins: annunciator panel, checklist, logbook.',
    license='BSD-3-Clause',
    entry_points={
        'console_scripts': [
            'annunciator = rqt_operator_tools.annunciator_standalone:main',
        ],
    },
)
