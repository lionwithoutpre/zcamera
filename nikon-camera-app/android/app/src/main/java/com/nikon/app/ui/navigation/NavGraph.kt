package com.nikon.app.ui.navigation

import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Collections
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Style
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material.icons.outlined.Collections
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Style
import androidx.compose.material.icons.outlined.SwapVert
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.nikon.app.ui.screens.*
import com.nikon.app.ui.theme.NikonBlack
import com.nikon.app.ui.theme.NikonSurface2
import com.nikon.app.ui.theme.NikonText2
import com.nikon.app.ui.theme.NikonText3
import com.nikon.app.ui.theme.NikonYellow
import com.nikon.app.viewmodel.CameraViewModel

/**
 * 导航路由常量
 */
object Routes {
    const val HOME     = "home"
    const val GALLERY  = "gallery"
    const val TRANSFER = "transfer"
    const val PRESET   = "preset"
    const val SETTINGS = "settings"
    const val LIVEVIEW = "liveview"
}

data class BottomTab(
    val route: String,
    val label: String,
    val selectedIcon: ImageVector,
    val unselectedIcon: ImageVector,
)

val bottomTabs = listOf(
    BottomTab(Routes.HOME,     "主页", Icons.Filled.Home,         Icons.Outlined.Home),
    BottomTab(Routes.GALLERY,  "相册", Icons.Filled.Collections,  Icons.Outlined.Collections),
    BottomTab(Routes.TRANSFER, "传输", Icons.Filled.SwapVert,     Icons.Outlined.SwapVert),
    BottomTab(Routes.PRESET,   "预设", Icons.Filled.Style,        Icons.Outlined.Style),
    BottomTab(Routes.SETTINGS, "设置", Icons.Filled.Settings,     Icons.Outlined.Settings),
)

@Composable
fun NikonNavGraph(
    viewModel: CameraViewModel = viewModel(),
    navController: NavHostController = rememberNavController(),
) {
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = navBackStackEntry?.destination

    // 全局 Snackbar 宿主 — 收集 errorEvents Channel(排队,不丢事件)
    val snackbarHostState = remember { SnackbarHostState() }
    LaunchedEffect(Unit) {
        viewModel.errorEvents.collect { msg ->
            snackbarHostState.showSnackbar(
                message = msg,
                actionLabel = "关闭",
                duration = SnackbarDuration.Short,
            )
        }
    }

    // 判断是否在 LiveView 全屏页面 (不显示底部导航栏)
    val showBottomBar = currentDestination?.hierarchy?.none { it.route == Routes.LIVEVIEW } ?: true

    Scaffold(
        containerColor = NikonBlack,
        snackbarHost = { SnackbarHost(snackbarHostState) },
        bottomBar = {
            if (showBottomBar) {
                NavigationBar(
                    containerColor = NikonSurface2,
                    contentColor = NikonYellow,
                ) {
                    bottomTabs.forEach { tab ->
                        val selected = currentDestination?.hierarchy?.any {
                            it.route == tab.route
                        } == true

                        NavigationBarItem(
                            selected = selected,
                            onClick = {
                                navController.navigate(tab.route) {
                                    popUpTo(navController.graph.findStartDestination().id) {
                                        saveState = true
                                    }
                                    launchSingleTop = true
                                    restoreState = true
                                }
                            },
                            icon = {
                                Icon(
                                    imageVector = if (selected) tab.selectedIcon else tab.unselectedIcon,
                                    contentDescription = tab.label
                                )
                            },
                            label = { Text(tab.label) },
                            colors = NavigationBarItemDefaults.colors(
                                selectedIconColor = NikonYellow,
                                selectedTextColor = NikonYellow,
                                unselectedIconColor = NikonText3,
                                unselectedTextColor = NikonText3,
                                indicatorColor = NikonYellow.copy(alpha = 0.12f),
                            )
                        )
                    }
                }
            }
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = Routes.HOME,
            modifier = Modifier.padding(innerPadding),
        ) {
            composable(Routes.HOME) {
                HomeScreen(viewModel = viewModel, onNavigateToLiveView = {
                    navController.navigate(Routes.LIVEVIEW)
                })
            }
            composable(Routes.GALLERY) {
                GalleryScreen(viewModel = viewModel)
            }
            composable(Routes.TRANSFER) {
                TransferScreen(viewModel = viewModel)
            }
            composable(Routes.PRESET) {
                PresetScreen(viewModel = viewModel)
            }
            composable(Routes.SETTINGS) {
                SettingsScreen(viewModel = viewModel)
            }
            composable(Routes.LIVEVIEW) {
                LiveViewScreen(
                    viewModel = viewModel,
                    onBack = { navController.popBackStack() },
                    onNavigateToGallery = { navController.navigate(Routes.GALLERY) },
                )
            }
        }
    }
}
