from setuptools import find_packages, setup

package_name = 'rqt_coverage'

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
    description='Survey-area coverage and quality scoreboard fed by the '
                'coverage_assessment node.',
    license='BSD-3-Clause',
    entry_points={
        'console_scripts': [
            'survey_coverage = rqt_coverage.coverage_standalone:main',
        ],
    },
)
